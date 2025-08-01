-- plugins/test_style.lua
editor_api.set_status_message("Loaded test_style.lua. Try commands:")

editor_api.register_event("on_key_press", function(key_code)
    if key_code == string.byte('1') then
        -- Set line 5 (0-indexed) from column 1 to 10 (exclusive) red
        editor_api.set_text_color(5, 1, 11, "#FF0000")
        editor_api.set_status_message("Applied red to line 5, chars 1-10")
        return true
    elseif key_code == string.byte('2') then
        -- Set line 5 from column 6 to 15 (exclusive) bold
        editor_api.set_text_style(5, 6, 16, editor_api.STYLE_BOLD)
        editor_api.set_status_message("Applied bold to line 5, chars 6-15")
        return true
    elseif key_code == string.byte('3') then
        -- Clear styling on line 5
        editor_api.clear_text_styling(5)
        editor_api.set_status_message("Cleared styling on line 5")
        return true
    end
    return false
end)