# Home Assistant integration (Stage 11)

The usermod exposes everything through WLED's `/json/state`, under a
`CeilingMover` object. Home Assistant drives it with `rest_command`s plus a few
input helpers. Ordinary colour/brightness/on-off still work through the normal
WLED integration entity — this only adds the *animated* behaviour and the RGBW
mode selector.

> `layout` is 0-based in the JSON API. The helpers below present 1-based choices
> and subtract 1.

## JSON API reference

Send `POST /json/state` with body:

```jsonc
// Go to a layout (animated with current motion preset)
{"CeilingMover":{"layout":1}}                  // 0-based: 1 == Layout 2
{"CeilingMover":{"layout":1,"noanim":true}}    // instant, no animation

// Animated ON  (edge: 0 left, 1 right, 2 both, 3 nearest, 4 none)
{"CeilingMover":{"on":{"edge":0,"layout":2}}}

// Animated OFF (edge as above)
{"CeilingMover":{"off":{"edge":2}}}

// RGBW auto-white mode: 2 Accurate, 1 Brighter, 4 Max
{"CeilingMover":{"awm":2}}

// Override motion parameters (any subset), applied before the action
{"CeilingMover":{"speed":700,"accel":0.4,"cruise":0.2,"decel":0.4,"fps":52,"layout":3}}
```

`edge` value `4` (none) and `noanim:true` are the "No animation" ON/OFF choices;
pick a random edge/layout in an HA script for the "random" choices — they are
ON/OFF *actions*, not layout presets.

## configuration.yaml

```yaml
rest_command:
  ceiling_goto:
    url: "http://WLED_IP/json/state"
    method: POST
    content_type: "application/json"
    payload: '{"CeilingMover":{"layout":{{ layout }},"noanim":{{ noanim }}}}'
  ceiling_on:
    url: "http://WLED_IP/json/state"
    method: POST
    content_type: "application/json"
    payload: '{"CeilingMover":{"on":{"edge":{{ edge }},"layout":{{ layout }}}}}'
  ceiling_off:
    url: "http://WLED_IP/json/state"
    method: POST
    content_type: "application/json"
    payload: '{"CeilingMover":{"off":{"edge":{{ edge }}}}}'
  ceiling_awm:
    url: "http://WLED_IP/json/state"
    method: POST
    content_type: "application/json"
    payload: '{"CeilingMover":{"awm":{{ awm }}}}'

input_select:
  ceiling_layout:
    name: Ceiling layout
    options: ["Layout 1", "Layout 2", "Layout 3", "Layout 4", "Layout 5"]
  ceiling_rgbw_mode:
    name: Ceiling RGBW mode
    options: ["Accurate", "Brighter", "Max"]

input_number:
  ceiling_max_speed:
    name: Ceiling max speed (px/s)
    min: 50
    max: 2000
    step: 10
```

## Scripts

```yaml
script:
  ceiling_goto_selected:
    sequence:
      - service: rest_command.ceiling_goto
        data:
          # input_select index -> 0-based layout
          layout: >
            {{ ['Layout 1','Layout 2','Layout 3','Layout 4','Layout 5']
               .index(states('input_select.ceiling_layout')) }}
          noanim: "false"

  ceiling_set_rgbw:
    sequence:
      - service: rest_command.ceiling_awm
        data:
          awm: >
            {% set m = states('input_select.ceiling_rgbw_mode') %}
            {{ 2 if m == 'Accurate' else 1 if m == 'Brighter' else 4 }}

  ceiling_on_nearest:
    sequence:
      - service: rest_command.ceiling_on
        data: { edge: 3, layout: 0 }   # 3 = nearest

  ceiling_off_both:
    sequence:
      - service: rest_command.ceiling_off
        data: { edge: 2 }              # 2 = both edges

  ceiling_random_layout:
    sequence:
      - service: rest_command.ceiling_goto
        data:
          layout: "{{ range(0,5) | random }}"
          noanim: "false"
```

Wire the `input_select` change events to `ceiling_goto_selected` /
`ceiling_set_rgbw` with automations, or call the scripts from dashboard buttons.
Changing ordinary colour/brightness continues to use the normal WLED light
entity unchanged.

## Optional: preset-based control

If you prefer WLED's own preset select entity (exposed by the HA WLED
integration), create WLED presets whose "API command" JSON is a `CeilingMover`
object (e.g. `{"CeilingMover":{"layout":2}}`). Selecting the preset in HA then
triggers the animated move with no `rest_command` needed. The `rest_command`
route above is more flexible for per-call parameter overrides.
