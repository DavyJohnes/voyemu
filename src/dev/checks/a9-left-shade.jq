def rect_ok($width; $height):
    .x >= 0 and .y >= 0 and .width > 0 and .height > 0
    and .x + .width <= $width and .y + .height <= $height;

def one:
    if length == 1 then .[0] else empty end;

def window_read($surface):
    if has("read_complete") then .read_complete == true
    else ($surface.coverage_basis == "accessibility_application_root"
        or $surface.coverage_basis == "verified_owned_vehicle_drawer")
        and $surface.scope == "api28_default_display"
        and $surface.unavailable_windows == []
    end;

def ancestor($index; $resource):
    .window_id as $window
    | first(limit(32; recurse(.parent as $parent | $index[$parent | tostring] // empty))
        | select(.resource_id == $resource and .window_id == $window));

.result
| select(.format == "voyemu.session.ui.v1")
| .multimedia[0]
| select(.screen == "primary" and .complete and .status == "ok")
| select(all(.boot_id, .source_instance, .revision; type == "string" and length > 0))
| . as $surface
| INDEX($surface.nodes[]; .id | tostring) as $nodes
| ($surface.windows | map(select(.package == $owner and (.type | tostring) == "3"
    and window_read($surface)))) as $overlays
| [$surface.nodes[] | select(.package == $owner and .visible
    and .resource_id == ($owner + ":id/tv_title")
    and (.text == "Electric" or .text == "Hybrid" or .text == "Save"
        or .text == "纯电优先" or .text == "油电混合" or .text == "保电模式")
    and (.clipped_bounds | rect_ok($surface.width; $surface.height))) | . as $title
    | ancestor($nodes; $owner + ":id/rl_root")
    | select(.package == $owner and .class == "android.widget.RelativeLayout"
        and .visible and .enabled and .clickable and .bounds == .clipped_bounds
        and (.clipped_bounds | rect_ok($surface.width; $surface.height))) as $card
    | ($card | ancestor($nodes; $owner + ":id/screen_up_vehicle_hiboard_new")
        | select(.package == $owner and .visible)) as $drawer
    | $overlays[] | select(.id == $title.window_id and .bounds.x == 0
        and .bounds.width > $surface.width / 2 and .active and .input_focused)
    | {title: $title, card: $card, drawer: $drawer, window: .}] as $cards
| if $stage == "open" then
    ($cards | one) as $card
    | {boot_id, source_instance, revision, observed_start_ns: .host_interval.start_ns,
        card: $card}
  else
    select($cards | length == 0)
    | select(all($surface.nodes[];
        .resource_id != ($owner + ":id/screen_up_vehicle_hiboard_new") or .visible != true))
    | ($surface.windows | map(select((.type | tostring) == "1" and .bounds.x == 0
        and .bounds.y == 0 and .bounds.width == $surface.width
        and .bounds.height == $surface.height)) | one)
        as $application
    | select($application.package == $foreground)
    | ([$surface.nodes[] | select(.package == $owner and .visible and .enabled
        and .resource_id == ($owner + ":id/line_view") and .class == "android.view.View"
        and .bounds == .clipped_bounds
        and (.clipped_bounds | rect_ok($surface.width; $surface.height))) | . as $node
        | $overlays[] | select(.id == $node.window_id and .bounds.x == 0
            and .bounds.height > .bounds.width)
        | {node: $node, window: .}] | one) as $handle
    | $handle.node.bounds as $bounds
    | ($bounds.x + ($bounds.width / 2 | floor)) as $start
    | ($surface.width / 2 | floor) as $end
    | select($end > $start)
    | {screen: "primary", observed_end_ns: .host_interval.end_ns,
        expected: ($surface | {boot_id, source_instance, revision} + {
            window: ($handle.window | {id, type, layer, bounds, title, package} | with_entries(select(.value != null))),
            target: ($handle.node | {window_id, resource_id, package, class, text, bounds, clipped_bounds} | with_entries(select(.value != null)))
        }),
        points: [range(0; 11) as $step | {
            x: ($start + (($end - $start) * $step / 10 | floor)),
            y: ($bounds.y + ($bounds.height / 2 | floor)),
            at_ms: ($step * 30)
        }]}
  end
