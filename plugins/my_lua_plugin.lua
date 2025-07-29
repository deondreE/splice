function on_load()
    -- This function is called by the editor when the plugin is loaded
    editor_api.set_status_message(
        "Lua Plugin 'my_lua_plugin' loaded successfully!",
        editor_api.get_current_time_ms()
    )
end

function greet_command()
    -- This function will be called when Editor::executeLuaPluginCommand("my_lua_plugin", "greet_command") is invoked
    local current_line = editor_api.get_current_line_text()
    local cursor_x = editor_api.get_cursor_x()
    local cursor_y = editor_api.get_cursor_y()

    editor_api.set_status_message(
        "Hello from Lua plugin! Line: '" .. current_line .. "' at (" .. cursor_x .. ", " .. cursor_y .. ")",
        editor_api.get_current_time_ms()
    )

    editor_api.insert_text("\n\n-- Text inserted by Lua plugin --")
    editor_api.refresh_screen() -- Ask editor to redraw
end