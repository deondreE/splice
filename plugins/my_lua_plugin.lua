-- plugins/my_lua_plugin.lua

-- The 'editor_api' global table is provided by the C++ editor
local editor = editor_api

function on_load()
    -- This function is called by the editor when the plugin is loaded
    -- Using the new `show_message` for better control over display duration.
    -- No need to pass get_current_time_ms(), as show_message handles the timer internally.
    editor.show_message("Lua Plugin 'my_lua_plugin' loaded successfully! Type Ctrl-K to duplicate line.", 3000) -- Show for 3 seconds

    -- Register a key press handler for a custom action (e.g., Ctrl-K for Duplicate Line)
    editor.register_event("on_key_press", function(key_code)
        -- In a real scenario, you'd check for specific key codes combined with modifier states.
        -- Assuming 'K' is 75 (for capital K) and 'ctrl_pressed' indicates Ctrl is held.
        -- This is a simplified check. You might need to adjust key_code based on your input system.
        if key_code == 75 and editor.is_ctrl_pressed() then -- Check for Ctrl+K (assuming 75 is K)
            editor.show_message("Ctrl-K pressed! Duplicating line...", 1500)

            local current_line_num = editor.get_cursor_y() -- This is 1-based from Lua's perspective
            local current_line_text = editor.get_line(current_line_num) -- Get the current line content
            local current_cursor_x = editor.get_cursor_x() -- Get cursor X

            -- Insert the duplicate line below the current one
            editor.insert_line(current_line_num + 1, current_line_text)

            -- Move cursor to the beginning of the newly duplicated line
            editor.set_cursor_position(1, current_line_num + 1) -- Set 1-based x, y

            return true -- Indicate that this key press was handled by the plugin, stop further processing
        end

        -- Check for Ctrl-D to trigger the `greet_command` directly from keybind
        if key_code == 68 and editor.is_ctrl_pressed() then -- Check for Ctrl+D (assuming 68 is D)
            editor.show_message("Ctrl-D pressed! Running greet_command...", 1500)
            greet_command()
            return true -- Handled
        end

        return false -- Not handled, let editor process it normally
    end)

    -- Register a handler for when a file is opened
    editor.register_event("on_file_opened", function(filename)
        editor.show_message("Just opened: " .. filename, 2500)
    end)

    -- Register a handler for when the buffer content changes (e.g., typing)
    editor.register_event("on_buffer_changed", function()
        -- You could run a linter here, or update a status display
        -- For demonstration, let's update a message every few changes (avoid too frequent updates)
        -- A real linter would debounce this or run in a background thread if Lua supported it easily.
        -- For now, just a simple message, to show it works.
        -- This would be very chatty, consider debouncing or specific triggers.
        -- editor.show_message("Buffer content changed!", 500)
    end)
end

function greet_command()
    -- This function can be called via a keybind (Ctrl-D in on_load) or Editor::executeLuaPluginCommand
    local cursor_x = editor.get_cursor_x() -- Now 1-based
    local cursor_y = editor.get_cursor_y() -- Now 1-based

    -- get_current_line_text() is still fine, but we can also use get_line(line_number)
    local current_line_text = editor.get_line(cursor_y)
    local filename = editor.get_current_filename() or "[No Name]" -- Handle no filename case

    editor.show_message(
        "Hello from Lua plugin! File: '" .. filename .. "' Line " .. cursor_y .. ", Col " .. cursor_x .. ". Content: '" .. current_line_text .. "'",
        5000 -- Display for 5 seconds
    )

    -- Insert text, including newlines, using the refined insert_text
    editor.insert_text("\n\n-- Text inserted by Lua plugin --\n")
    editor.insert_text("File was: " .. filename .. "\n")
    editor.insert_text("Cursor was at: Line " .. cursor_y .. ", Col " .. cursor_x .. "\n")

    -- Save a piece of plugin data
    editor.save_plugin_data("last_greet_time", tostring(editor.get_current_time_ms()))
    editor.show_message("Last greet time saved.", 2000)

    -- Example: load some data
    local last_time_str = editor.load_plugin_data("last_greet_time")
    if last_time_str then
        editor.show_message("Previous greet was at: " .. last_time_str .. " ms", 3000)
    end

    -- No need for editor.refresh_screen() after insertions if insert_text already handles it.
    -- If insert_text just modifies buffer and doesn't trigger refresh, you'd keep it.
    -- (The C++ insert_text should trigger necessary updates and potentially a partial redraw).
    -- However, a full refresh is often harmless after major operations like this.
    editor.refresh_screen()
end

-- Example of a command to toggle the terminal (callable via Editor::executeLuaPluginCommand)
function toggle_terminal_command()
    editor.toggle_terminal()
    editor.show_message("Terminal toggled!", 2000)
end

-- Example: Search and navigate to next match
function find_and_go_next(search_term)
    search_term = search_term or "Lua" -- Default search term

    local line_count = editor.get_line_count()
    local matches = {}

    for i = 1, line_count do
        local line_text = editor.get_line(i)
        local start_pos = 1
        while true do
            local pos = string.find(line_text, search_term, start_pos, true) -- plain find
            if pos then
                table.insert(matches, {line = i, col = pos})
                start_pos = pos + 1 -- Move past current match
            else
                break
            end
        end
    end

    if #matches > 0 then
        -- Find the first match after the current cursor position
        local current_y = editor.get_cursor_y()
        local current_x = editor.get_cursor_x()

        local found_next = false
        for _, match_info in ipairs(matches) do
            if (match_info.line > current_y) or (match_info.line == current_y and match_info.col > current_x) then
                editor.set_cursor_position(match_info.col, match_info.line)
                editor.center_view_on_cursor()
                editor.show_message("Found next match: Line " .. match_info.line .. ", Col " .. match_info.col, 3000)
                found_next = true
                break
            end
        end

        if not found_next then
            -- Wrap around to the first match if no further match found
            local first_match = matches[1]
            editor.set_cursor_position(first_match.col, first_match.line)
            editor.center_view_on_cursor()
            editor.show_message("Wrapped around to first match: Line " .. first_match.line .. ", Col " .. first_match.col, 3000)
        end
    else
        editor.show_error("No matches found for '" .. search_term .. "'", 3000)
    end
end

-- Example: Prompt the user for input
function get_user_input_example()
    editor.show_message("Awaiting user input via prompt...", 5000)

    -- In a *real* blocking prompt, this would pause Lua execution.
    -- With our current simple `lua_prompt_user`, this will *not* block.
    -- It will just set the prompt and immediately return.
    -- To make it blocking, you'd need to use Lua coroutines in your C++ `lua_prompt_user`
    -- and `Editor::promptUser` logic.
    -- For now, this is just for demonstration of how to call it.
    local prompt_started = editor.prompt("Enter some text: ", "default value")

    if prompt_started then
        editor.show_message("Prompt initiated. Type in the message bar and press Enter.", 4000)
        -- The result will be available via another mechanism or a callback from C++
        -- when the prompt is actually finished.
        -- For a truly blocking experience, the Lua side would yield here.
    else
        editor.show_error("Failed to start prompt!", 2000)
    end
end

-- You can also call other global functions directly from on_load if you wish
-- on_load runs after the plugin is loaded, useful for initial setup
-- editor.show_message("Try 'greet_command()' in your config or bind it to a key!", 5000)