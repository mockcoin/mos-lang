-- Lua 5.5 Performance Test: Table and JSON operations
-- Same logic as Mos test for performance comparison

-- Simple JSON encode/decode implementation
local json = {}

function json.encode(t)
    local function encode_val(v, indent)
        local t = type(v)
        if t == "nil" then
            return "null"
        elseif t == "boolean" then
            return v and "true" or "false"
        elseif t == "number" then
            if v == math.floor(v) then
                return tostring(v)
            else
                return string.format("%.17g", v)
            end
        elseif t == "string" then
            return string.format("%q", v):gsub("\\\n", "\\n")
        elseif t == "table" then
            local result = "{"
            local first = true
            for k, val in pairs(v) do
                if not first then result = result .. "," end
                local key_str
                if type(k) == "number" then
                    key_str = "[" .. tostring(k) .. "]"
                else
                    key_str = string.format("%q", k)
                end
                result = result .. key_str .. ":" .. encode_val(val, indent)
                first = false
            end
            return result .. "}"
        else
            return "null"
        end
    end
    return encode_val(t, "")
end

function json.decode(s)
    local pos = 1
    local function skip_spaces()
        while pos <= #s and s:match("^%s", pos) do
            pos = pos + 1
        end
    end
    
    local function parse_value()
        skip_spaces()
        if pos > #s then return nil end
        local c = s:sub(pos, pos)
        if c == '"' then
            return parse_string()
        elseif c == '{' then
            return parse_object()
        elseif c == '[' then
            return parse_array()
        elseif c == 't' then
            pos = pos + 4
            return true
        elseif c == 'f' then
            pos = pos + 5
            return false
        elseif c == 'n' then
            pos = pos + 4
            return nil
        else
            return parse_number()
        end
    end
    
    function parse_string()
        local start = pos
        pos = pos + 1
        while pos <= #s do
            local c = s:sub(pos, pos)
            if c == '"' then
                local str = s:sub(start + 1, pos - 1)
                pos = pos + 1
                return str
            elseif c == '\\' then
                pos = pos + 2
            else
                pos = pos + 1
            end
        end
        return nil
    end
    
    function parse_number()
        local num_str = s:match("^-?%d+%.?%d*[eE]?[+-]?%d*", pos)
        if num_str then
            pos = pos + #num_str
            return tonumber(num_str)
        end
        return nil
    end
    
    function parse_object()
        local obj = {}
        pos = pos + 1
        skip_spaces()
        if s:sub(pos, pos) == '}' then
            pos = pos + 1
            return obj
        end
        while pos <= #s do
            skip_spaces()
            local key = parse_string()
            skip_spaces()
            pos = pos + 1
            local val = parse_value()
            obj[key] = val
            skip_spaces()
            local c = s:sub(pos, pos)
            if c == '}' then
                pos = pos + 1
                break
            elseif c == ',' then
                pos = pos + 1
            end
        end
        return obj
    end
    
    function parse_array()
        local arr = {}
        pos = pos + 1
        skip_spaces()
        if s:sub(pos, pos) == ']' then
            pos = pos + 1
            return arr
        end
        local idx = 1
        while pos <= #s do
            arr[idx] = parse_value()
            idx = idx + 1
            skip_spaces()
            local c = s:sub(pos, pos)
            if c == ']' then
                pos = pos + 1
                break
            elseif c == ',' then
                pos = pos + 1
            end
        end
        return arr
    end
    
    return parse_value()
end

-- Performance tests
print("=== Lua 5.5 Performance Test ===")

-- Test 1: Table set/get operations
start_time = os.clock()
m = {}
count = 10000
for i = 1, count do
    m["key" .. tostring(i)] = i
end
for i = 1, count do
    local val = m["key" .. tostring(i)]
end
t1 = os.clock() - start_time
print("Table set/get " .. tostring(count) .. " times: " .. tostring(t1) .. "s")

-- Test 2: Simple JSON encode
print("\n=== Test 2: JSON encode ===")
simple = {name = "test", value = 42, active = true}
start_time = os.clock()
for i = 1, 100 do
    json_str = json.encode(simple)
end
t2 = os.clock() - start_time
print("JSON encode 100 times: " .. tostring(t2) .. "s")

-- Test 3: JSON decode
print("\n=== Test 3: JSON decode ===")
json_input = '{"name":"test","value":42,"active":true}'
start_time = os.clock()
for i = 1, 100 do
    decoded = json.decode(json_input)
end
t3 = os.clock() - start_time
print("JSON decode 100 times: " .. tostring(t3) .. "s")

-- Test 4: Table merge
print("\n=== Test 4: Table merge ===")
m1 = {}
m2 = {}
for i = 1, 1000 do
    m1["a" .. tostring(i)] = i
    m2["b" .. tostring(i)] = i * 2
end
start_time = os.clock()
merged = {}
for k, v in pairs(m1) do merged[k] = v end
for k, v in pairs(m2) do merged[k] = v end
t4 = os.clock() - start_time
print("Table merge 1000+1000 items: " .. tostring(t4) .. "s")

print("\n=== Summary ===")
print("Table set/get (10000 items): " .. tostring(t1) .. "s")
print("JSON encode (100 times): " .. tostring(t2) .. "s")
print("JSON decode (100 times): " .. tostring(t3) .. "s")
print("Table merge (2000 items): " .. tostring(t4) .. "s")
