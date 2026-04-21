-- Claude Sonnet 4.6
-- KeyPress Protobuf/UDP Dissector for ESP32-C6
-- Proto: KeyPress { key_id (uint32, tag=1), state (uint32, tag=2), ts (uint32, tag=3) }
-- Port: 4210 (broadcast)
--
-- Install: copy to Wireshark Personal Lua Plugins folder
-- (Help → About Wireshark → Folders → Personal Lua Plugins)
-- Reload: Ctrl+Shift+L  or restart Wireshark

local proto_keypress = Proto("keypress", "ESP32 KeyPress (Protobuf)")

-- Protocol fields
local f_key_id = ProtoField.uint32("keypress.key_id", "Key ID",    base.DEC)
local f_state  = ProtoField.uint32("keypress.state",  "State",     base.DEC)
local f_ts     = ProtoField.uint32("keypress.ts",     "Timestamp", base.DEC)

proto_keypress.fields = { f_key_id, f_state, f_ts }

-- Decode a protobuf varint from tvb at offset
-- Returns: (value, new_offset)
local function decode_varint(tvb, offset)
    local value = 0
    local shift = 0
    local byte
    repeat
        if offset >= tvb:len() then
            error("varint extends beyond buffer")
        end
        byte  = tvb(offset, 1):uint()
        value = value + bit.lshift(bit.band(byte, 0x7F), shift)
        shift  = shift + 7
        offset = offset + 1
    until bit.band(byte, 0x80) == 0
    return value, offset
end

function proto_keypress.dissector(tvb, pinfo, tree)
    local pkt_len = tvb:len()
    if pkt_len == 0 then return end

    pinfo.cols.protocol = "KeyPress"
    pinfo.cols.info     = "ESP32 KeyPress"

    local subtree = tree:add(proto_keypress, tvb(), "KeyPress Message")
    local offset  = 0

    local fields_seen = {}

    while offset < pkt_len do
        local ok, tag_raw, new_offset = pcall(decode_varint, tvb, offset)
        if not ok then
            subtree:add_expert_info(PI_MALFORMED, PI_ERROR, "Bad varint at offset " .. offset)
            break
        end
        offset = new_offset

        local field_number = bit.rshift(tag_raw, 3)
        local wire_type    = bit.band(tag_raw, 0x07)

        -- All three fields are uint32 → wire type 0 (varint)
        if wire_type == 0 then
            local val
            ok, val, offset = pcall(function()
                local v, o = decode_varint(tvb, offset)
                return v, o
            end)
            -- pcall with multiple returns needs a wrapper like this:
            local v, o
            ok = pcall(function() v, o = decode_varint(tvb, offset) end)
            if not ok then
                subtree:add_expert_info(PI_MALFORMED, PI_ERROR, "Bad varint value at offset " .. offset)
                break
            end
            offset = o

            if field_number == 1 then
                subtree:add(f_key_id, v)
                pinfo.cols.info = string.format("KeyPress  key_id=%d  state=%s  ts=%s",
                    v,
                    fields_seen[2] or "?",
                    fields_seen[3] or "?")
            elseif field_number == 2 then
                subtree:add(f_state, v)
            elseif field_number == 3 then
                subtree:add(f_ts, v)
            else
                subtree:add(tvb(offset - 1, 1),
                    string.format("Unknown field %d (varint) = %d", field_number, v))
            end
            fields_seen[field_number] = tostring(v)

        else
            -- Unexpected wire type for this message
            subtree:add_expert_info(PI_MALFORMED, PI_WARN,
                string.format("Unexpected wire type %d for field %d at offset %d",
                    wire_type, field_number, offset - 1))
            break
        end
    end

    -- Build a clean info column once all fields are parsed
    pinfo.cols.info = string.format(
        "KeyPress  key_id=%-4s  state=%-2s  ts=%s",
        fields_seen[1] or "?",
        fields_seen[2] or "?",
        fields_seen[3] or "?"
    )
end

-- Register on UDP port 4210 (broadcast traffic hits the same port)
DissectorTable.get("udp.port"):add(4210, proto_keypress)