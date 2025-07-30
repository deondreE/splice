local editor = editor_api

-- Set global editor preferences
editor.set_tab_stop_width(4) -- Needs new Lua API: editor.set_tab_stop_width(width)
editor.set_default_line_ending("LF") -- Needs new Lua API: editor.set_default_line_ending(type)

-- Default font if available (optional)
local success = editor.set_console_font("Consolas", 10, 20)
if not success then
    editor.show_error("Failed to set default font from config.lua", 2000)
end

-- Keybinding overrides (example)
editor.register_event("on_key_press", function(key_code)
    -- Override Ctrl-S to do something else, or call multiple functions
    if key_code == 19 and editor.is_ctrl_pressed() then -- Ctrl-S (ASCII for 'S')
        editor.show_message("Custom Ctrl-S action!", 1000)
        -- editor.save_file() -- Still save
        return true -- Handled
    end
    return false
end)