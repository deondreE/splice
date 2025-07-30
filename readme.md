# Splice

An editor that is meant to be fast and has no issues doing it.

There is a plugin system, it is terminal based, if you submit a AI plugin it will be removed and the way you made it work will be deprecated.

For windows use only. For Now.

# Lua Plugin Development

Splice's true power lies in its Lua scripting capabilities. You can write plugins to:

- Add custom keybindings and commands.
- Implement advanced text transformations (e.g., linters, formatters).
- Create new interactive functionalities.
- Automate repetitive tasks.
- Integrate with external command-line tools.

### Plugin Directory

Place your Lua plugin files (.lua) inside a plugins/ directory, located in the same directory as the splice.exe executable.

```md
.
├── splice.exe
└── plugins/
├── my_first_plugin.lua
└── utility_scripts/
└── formatter.lua
```

### The `on_load()` Function

Each plugin can define a global Lua function named on_load(). If this function is present in your plugin file, the editor will automatically call it when the plugin is loaded. This is the ideal place for any plugin initialization, registering event handlers, or setting up initial configurations.

```lua
-- plugins/my_init_plugin.lua
function on_load()
    editor_api.show_message("My initialization plugin loaded!", 2000)
    -- Register event handlers or perform setup here
end
```

### The editor_api Global Table

Inside your Lua plugins, the editor exposes its functionality through a global table named editor_api. It's a common and recommended practice to alias this for convenience, for example:

```lua
 local editor = editor_api
```

All functions and properties available to plugins are methods of this editor table.

### Example Plugin: simple_actions.lua

This example demonstrates how to set up keybindings, interact with the buffer, and use the status messages.

plugins/simple_actions.lua

```lua
-- plugins/simple_actions.lua

-- Alias the editor API for convenience
local editor = editor_api

-- Called automatically when the plugin loads
function on_load()
    editor.show_message("Plugin 'Simple Actions' loaded successfully! Try Ctrl-K or Ctrl-D.", 3000)

    -- Register a key press handler for custom actions
    -- This function will be called *before* the editor's default key processing.
    editor.register_event("on_key_press", function(key_code)
        -- Check for Ctrl-K: Duplicate Current Line
        -- Key codes might vary slightly depending on your input system.
        -- Assuming 'K' is 75 (uppercase K) and editor.is_ctrl_pressed() works.
        if key_code == 75 and editor.is_ctrl_pressed() then -- Ctrl+K
            editor.show_message("Ctrl-K pressed: Duplicating line...", 1000)

            local current_line_num = editor.get_cursor_y() -- 1-based line number
            local current_line_text = editor.get_line(current_line_num) -- Get content of current line

            -- Insert the duplicate line directly below the current one
            editor.insert_line(current_line_num + 1, current_line_text)

            -- Move the cursor to the beginning of the newly duplicated line
            editor.set_cursor_position(1, current_line_num + 1) -- Set 1-based column (1st char), and new line

            return true -- Indicate that this key press was handled by the plugin
        end

        -- Check for Ctrl-D: Run the 'greet_command'
        if key_code == 68 and editor.is_ctrl_pressed() then -- Ctrl+D
            editor.show_message("Ctrl-D pressed: Running greet_command...", 1000)
            greet_command() -- Call the command defined below
            return true -- Handled
        end

        return false -- Not handled, let the editor process the key normally
    end)

    -- Register an event handler for when a file is opened
    editor.register_event("on_file_opened", function(filename)
        editor.show_message("File opened: " .. filename, 2500)
    end)

    -- Example: Register for buffer changes (useful for linters/formatters)
    -- This can be very chatty if not debounced!
    -- editor.register_event("on_buffer_changed", function()
    --    editor.show_message("Buffer modified!", 500)
    -- end)
end

-- Define a custom command that can be called by keybinds or other Lua scripts
function greet_command()
    local cursor_x = editor.get_cursor_x() -- 1-based column
    local cursor_y = editor.get_cursor_y() -- 1-based line

    local current_line_text = editor.get_line(cursor_y) -- Get the text of the line at cursor_y
    local filename = editor.get_current_filename() or "[No Name]" -- Get the current filename

    editor.show_message(
        "Hello from Lua! File: '" .. filename .. "' @ Line " .. cursor_y .. ", Col " .. cursor_x .. ". Text: '" .. current_line_text .. "'",
        5000 -- Display message for 5 seconds
    )

    -- Insert some descriptive text into the buffer
    editor.insert_text("\n\n-- Text inserted by Splice Lua plugin --\n")
    editor.insert_text("File: " .. filename .. "\n")
    editor.insert_text("Cursor was: Line " .. cursor_y .. ", Col " .. cursor_x .. "\n")

    -- Save and load some simple plugin data
    local timestamp = editor.get_current_time_ms()
    editor.save_plugin_data("last_greet_timestamp", tostring(timestamp))
    editor.show_message("Last greet time saved.", 1500)

    local loaded_timestamp_str = editor.load_plugin_data("last_greet_timestamp")
    if loaded_timestamp_str then
        editor.show_message("Last greet was at: " .. loaded_timestamp_str .. "ms.", 2500)
    end

    editor.refresh_screen() -- Request a screen refresh to show the inserted text
end

-- Example of a command to toggle the integrated terminal
function toggle_terminal_command()
    editor.toggle_terminal()
    editor.show_message("Terminal toggled!", 2000)
end

-- Example: Implement a basic "Find Next"
function find_next_occurrence(search_term)
    search_term = search_term or "Lua" -- Default search term if none provided

    local line_count = editor.get_line_count()
    local current_y = editor.get_cursor_y()
    local current_x = editor.get_cursor_x()

    local found_match = false

    -- Search from current cursor position to end of file
    for line_num = current_y, line_count do
        local line_text = editor.get_line(line_num)
        local start_search_col = 1
        if line_num == current_y then
            start_search_col = current_x + 1 -- Start search *after* current cursor for current line
        end

        local match_col = string.find(line_text, search_term, start_search_col, true) -- 'true' for plain search (no patterns)

        if match_col then
            editor.set_cursor_position(match_col, line_num)
            editor.center_view_on_cursor()
            editor.show_message("Found next match: Line " .. line_num .. ", Col " .. match_col .. " ('" .. search_term .. "')", 3000)
            found_match = true
            break
        end
    end

    if not found_match then
        -- If no match found after current position, wrap around and search from beginning
        for line_num = 1, current_y do
            local line_text = editor.get_line(line_num)
            local search_end_col = -1 -- Search whole line if before current line
            if line_num == current_y then
                search_end_col = current_x -- Stop search *at* current cursor for wrap-around
            end

            local match_col = string.find(line_text, search_term, 1, true)
            if match_col and (search_end_col == -1 or match_col <= search_end_col) then
                editor.set_cursor_position(match_col, line_num)
                editor.center_view_on_cursor()
                editor.show_message("Wrapped around to first match: Line " .. line_num .. ", Col " .. match_col .. " ('" .. search_term .. "')", 3000)
                found_match = true
                break
            end
        end
    end

    if not found_match then
        editor.show_error("No matches found for '" .. search_term .. "' in the entire buffer.", 3000)
    end
end
```

See [Lua Reference for More API info](./markdown/api.md)
