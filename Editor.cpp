#include "editor.h"
#include <sstream>
#include <algorithm>
#include "win_console_utils.h"
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

Editor::Editor() : cursorX(0), cursorY(0), screenRows(0), screenCols(0),
    rowOffset(0), colOffset(0), lineNumberWidth(0),
    statusMessage("HELP: Ctrl-Q = quit | Ctrl-S = save | Ctrl-O = open"),
    statusMessageTime(GetTickCount64()),
    prevStatusMessage(""), prevMessageBarMessage(""), dirty(false),
    mode(EDIT_MODE), selectedFileIndex(0), fileExporerScrollOffset(0),
    currentMatchIndex(-1), originalCursorX(0), originalCursorY(0),
    originalColOffset(0), originalRowOffset(0)
{
    lines.push_back("");
    updateScreenSize();
    prevDrawnLines.resize(screenRows - 2);
    for (auto& s : prevDrawnLines) s.assign(screenCols, ' ');
    
    char buffer[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, buffer);
    currentDirPath = buffer;
}

void Editor::calculateLineNumberWidth()
{
    size_t effectiveLineNumbers = std::max(static_cast<size_t>(1), lines.size());
    int numLines = static_cast<int>(effectiveLineNumbers);

    int digits = 0;
    if (numLines == 0) digits = 1;
    else {
        int temp = numLines;
        while (temp > 0) {
            temp /= 10;
            ++digits;
        }
    }
    lineNumberWidth = digits + 1;
}

void Editor::updateScreenSize() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi); // Use GetStdHandle locally
    int newScreenRows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    int newScreenCols = csbi.srWindow.Right - csbi.srWindow.Left + 1;

    if (newScreenRows != screenRows || newScreenCols != screenCols)
    {
        screenRows = newScreenRows;
        screenCols = newScreenCols;
        calculateLineNumberWidth();

        prevDrawnLines.resize(screenRows - 2);
        for (auto& s : prevDrawnLines)
        {
            if (s.length() != screenCols)
            {
                s.assign(screenCols, ' ');
            }
        }
        clearScreen();
        prevStatusMessage = "";
        prevMessageBarMessage = "";
    }
}

void Editor::drawScreenContent() {
    int effectiveScreenCols = screenCols - lineNumberWidth;

    for (int i = 0; i < screenRows - 2; ++i) {
        setCursorPosition(0, i);

        int fileRow = rowOffset + i;
        std::string lineToDraw;

        std::ostringstream ss_lineNumber;
        if (fileRow < lines.size()) {
            ss_lineNumber << std::setw(lineNumberWidth - 1) << (fileRow + 1) << " ";
        } else {
            ss_lineNumber << std::string(lineNumberWidth - 1, ' ') << "~";
        }
        lineToDraw += ss_lineNumber.str();

        std::string renderedTextContent = "";
        if (fileRow < lines.size()) {
            std::string fullRenderedLine = getRenderedLine(fileRow);

            int startCharInRenderedLine = colOffset;
            int endCharInRenderedLine = colOffset + effectiveScreenCols;

            if (startCharInRenderedLine < 0) startCharInRenderedLine = 0;
            if (startCharInRenderedLine > (int)fullRenderedLine.length()) startCharInRenderedLine = (int)fullRenderedLine.length();

            if (endCharInRenderedLine > (int)fullRenderedLine.length()) endCharInRenderedLine = (int)fullRenderedLine.length();
            if (endCharInRenderedLine < 0) endCharInRenderedLine = 0;

            if (endCharInRenderedLine > startCharInRenderedLine) {
                renderedTextContent = fullRenderedLine.substr(startCharInRenderedLine, endCharInRenderedLine - startCharInRenderedLine);
            }
        }

        bool lineHasHighlight = false;
        if (!searchQuery.empty() && currentMatchIndex != -1) {
            // Check if any match is on this fileRow
            for (const auto& match : searchResults) {
                if (match.first == fileRow) {
                    lineHasHighlight = true;
                    break;
                }
            }
        }
        
        std::string fullLineContentForDiff = lineToDraw + renderedTextContent + std::string(screenCols - (lineToDraw.length() + renderedTextContent.length()), ' ');

        if (lineHasHighlight || i >= prevDrawnLines.size() || prevDrawnLines[i] != fullLineContentForDiff) {
            setCursorPosition(0, i);
            resetTextColor(); // Default color for text area

            // Draw line number part first
            writeStringAt(0, i, lineToDraw.substr(0, lineNumberWidth));

            // Now iterate through the visible part of the rendered text content for highlighting
            int currentRenderedCol = lineNumberWidth;
            for (int k = 0; k < renderedTextContent.length(); ++k) {
                char ch = renderedTextContent[k];
                bool isHighlight = false;
                
                // Check if current char is part of the highlighted search result
                if (!searchQuery.empty() && currentMatchIndex != -1 && fileRow == searchResults[currentMatchIndex].first) {
                    // Convert search result char index to rendered index to compare
                    int matchRenderedStart = cxToRx(fileRow, searchResults[currentMatchIndex].second);
                    int matchRenderedEnd = cxToRx(fileRow, searchResults[currentMatchIndex].second + searchQuery.length());

                    // Relative to visible screen (colOffset)
                    if (currentRenderedCol - lineNumberWidth + colOffset >= matchRenderedStart &&
                        currentRenderedCol - lineNumberWidth + colOffset < matchRenderedEnd) {
                        isHighlight = true;
                    }
                }

                if (isHighlight) {
                    setTextColor(BG_YELLOW | BLACK); // Highlight: Black on Yellow
                } else {
                    resetTextColor(); // Default color
                }
                writeStringAt(currentRenderedCol, i, std::string(1, ch)); // Write one character
                currentRenderedCol++;
            }
            resetTextColor(); // Reset after text
            
            // Clear rest of the line to ensure old highlights are gone
            writeStringAt(currentRenderedCol, i, std::string(screenCols - currentRenderedCol, ' '));
            
            if (i < prevDrawnLines.size()) {
                prevDrawnLines[i] = fullLineContentForDiff; // Update prevDrawnLines with the full rendered line (no colors)
            }
        }
    }
    drawStatusBar();
    drawMessageBar();
}

void Editor::startSearch() {
    originalCursorX = cursorX; // Save original cursor position
    originalCursorY = cursorY;
    originalRowOffset = rowOffset;
    originalColOffset = colOffset;

    mode = PROMPT_MODE;
    promptMessage = "Search: ";
    searchQuery = ""; // Clear previous search query
    searchResults.clear(); // Clear previous results
    currentMatchIndex = -1; // No match selected yet
    statusMessage = "Enter search term. ESC to cancel, Enter to search.";
    statusMessageTime = GetTickCount64();

    // Invalidate prevDrawnLines to ensure prompt is drawn fresh
    for(auto& s : prevDrawnLines) s.assign(screenCols, ' ');
    prevStatusMessage = "";
    prevMessageBarMessage = "";
    clearScreen(); // Force full screen redraw to show prompt cleanly
}

void Editor::performSearch() {
    searchResults.clear();
    currentMatchIndex = -1;

    if (searchQuery.empty()) {
        statusMessage = "Search cancelled or empty.";
        statusMessageTime = GetTickCount64();
        mode = EDIT_MODE; // Exit search mode if query is empty
        cursorX = originalCursorX; // Restore cursor
        cursorY = originalCursorY;
        rowOffset = originalRowOffset; // Restore scroll
        colOffset = originalColOffset;
        return;
    }

    // Iterate through lines to find all matches
    for (int r = 0; r < lines.size(); ++r) {
        size_t pos = lines[r].find(searchQuery, 0);
        while (pos != std::string::npos) {
            searchResults.push_back({r, (int)pos});
            pos = lines[r].find(searchQuery, pos + 1); // Find next occurrence after current match
        }
    }

    if (searchResults.empty()) {
        statusMessage = "No matches found for '" + searchQuery + "'";
        statusMessageTime = GetTickCount64();
        mode = EDIT_MODE; // No matches, go back to edit mode
        cursorX = originalCursorX; // Restore cursor
        cursorY = originalCursorY;
        rowOffset = originalRowOffset; // Restore scroll
        colOffset = originalColOffset;
        return;
    }

    // Found matches. Go to the first one.
    currentMatchIndex = 0;
    statusMessage = "Found " + std::to_string(searchResults.size()) + " matches. (N)ext (P)rev";
    statusMessageTime = GetTickCount64();

    // Move cursor and scroll to the first match
    cursorY = searchResults[currentMatchIndex].first;
    cursorX = searchResults[currentMatchIndex].second;
    
    // Adjust scroll to make sure the found match is visible
    scroll(); // This should adjust rowOffset/colOffset
    
    mode = EDIT_MODE; // Exit search prompt mode
}


void Editor::findNext() {
    if (searchResults.empty()) return;

    currentMatchIndex = (currentMatchIndex + 1) % searchResults.size();

    // Move cursor and scroll to the new match
    cursorY = searchResults[currentMatchIndex].first;
    cursorX = searchResults[currentMatchIndex].second;
    scroll(); // Adjust scroll

    statusMessage = "Match " + std::to_string(currentMatchIndex + 1) + " of " + std::to_string(searchResults.size());
    statusMessageTime = GetTickCount64();
}

void Editor::findPrevious() {
    if (searchResults.empty()) return;

    currentMatchIndex--;
    if (currentMatchIndex < 0) {
        currentMatchIndex = searchResults.size() - 1;
    }

    cursorY = searchResults[currentMatchIndex].first;
    cursorX = searchResults[currentMatchIndex].second;
    scroll();

    statusMessage = "Match " + std::to_string(currentMatchIndex + 1) + " of " + std::to_string(searchResults.size());
    statusMessageTime = GetTickCount64();
}

bool Editor::promptUser(const std::string& prompt, int input_c, std::string& result) {
    if (input_c == 13) { // Enter
        mode = EDIT_MODE; // Exit prompt mode
        return true; // Success
    } else if (input_c == 27) { // Escape
        mode = EDIT_MODE; // Exit prompt mode
        result.clear(); // Clear the input
        statusMessage = "Operation cancelled.";
        statusMessageTime = GetTickCount64();
        cursorX = originalCursorX; // Restore cursor
        cursorY = originalCursorY;
        rowOffset = originalRowOffset; // Restore scroll
        colOffset = originalColOffset;
        return false; // Cancelled
    } else if (input_c == 8) { // Backspace
        if (!result.empty()) {
            result.pop_back();
        }
    } else if (input_c >= 32 && input_c <= 126) { // Printable characters
        result += static_cast<char>(input_c);
    }
    // Update the message bar with the current prompt and input
    statusMessage = prompt + result;
    statusMessageTime = GetTickCount64(); // Keep message alive while typing

    // Return false to indicate prompt mode is still active
    return false;
}

void Editor::drawStatusBar() {
    std::string currentStatus;
    std::string filename_display = filename.empty() ? "[No Name]" : filename;
    currentStatus += filename_display;
    currentStatus += " - " + std::to_string(lines.size()) + " lines";

    std::string cursor_info = std::to_string(cursorY + 1) + "/" + std::to_string(lines.size());
    std::string spaces(screenCols - currentStatus.length() - cursor_info.length(), ' ');

    currentStatus += spaces + cursor_info;

    // --- Only redraw if status bar content has changed ---
    if (currentStatus != prevStatusMessage) {
        setCursorPosition(0, screenRows - 2); // Second to last row
        setTextColor(BG_BLUE | WHITE);
        writeStringAt(0, screenRows - 2, currentStatus);
        prevStatusMessage = currentStatus;
    }
}

void Editor::drawMessageBar() {
    std::string currentMessage = "";
    ULONGLONG currentTime = GetTickCount64();
    if (currentTime - statusMessageTime < 5000) { // Display for 5 seconds
        currentMessage = trimRight(statusMessage);
    }
    currentMessage += std::string(screenCols - currentMessage.length(), ' ');

    // --- Only redraw if message bar content has changed ---
    if (currentMessage != prevMessageBarMessage) {
        setCursorPosition(0, screenRows - 1); // Last row
        setTextColor(YELLOW | INTENSITY);
        writeStringAt(0, screenRows - 1, currentMessage);
        prevMessageBarMessage = currentMessage;
    }
}

// Helper to trim trailing spaces (useful for status message display)
std::string Editor::trimRight(const std::string& s) {
    size_t end = s.find_last_not_of(" \t\n\r\f\v");
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

void Editor::scroll() {
    // ... vertical scrolling (unchanged) ...
    if (cursorY < rowOffset) {
        rowOffset = cursorY;
        for (auto& s : prevDrawnLines) s.assign(screenCols, ' ');
    }
    if (cursorY >= rowOffset + screenRows - 2) {
        rowOffset = cursorY - (screenRows - 2) + 1;
        for (auto& s : prevDrawnLines) s.assign(screenCols, ' ');
    }

    // --- MODIFIED: Horizontal scrolling to use rendered columns ---
    int renderedCursorX = cxToRx(cursorY, cursorX); // Get visual position of cursor
    int renderedLineLength = getRenderedLine(cursorY).length(); // Get visual length of current line

    if (renderedCursorX < colOffset) {
        colOffset = renderedCursorX;
        for (auto& s : prevDrawnLines) s.assign(screenCols, ' ');
    }
    if (renderedCursorX >= colOffset + (screenCols - lineNumberWidth)) {
        colOffset = renderedCursorX - (screenCols - lineNumberWidth) + 1;
        for (auto& s : prevDrawnLines) s.assign(screenCols, ' ');
    }
}

void Editor::moveCursor(int key) {
    int oldCursorX = cursorX;
    int oldCursorY = cursorY;
    std::string currentLine = (cursorY < lines.size()) ? lines[cursorY] : lines.front();

    switch (key) {
    case ARROW_LEFT:
        if (cursorX > 0) {
            // To move left, decrement cursorX.
            // If the character at cursorX-1 is a tab,
            // this is fine for internal storage.
            cursorX--;
        }
        else if (cursorY > 0) {
            cursorY--;
            cursorX = lines[cursorY].length(); // Move to end of previous line (char index)
        }
        break;
    case ARROW_RIGHT:
        if (cursorY < lines.size() && cursorX < currentLine.length()) {
            // To move right, increment cursorX.
            // This correctly moves to the next character index.
            cursorX++;
        }
        else if (cursorY < lines.size() - 1) { // Move to beginning of next line
            cursorY++;
            cursorX = 0;
        }
        break;
    case ARROW_UP:
        if (cursorY > 0) {
            // When moving up/down, we want the cursor to try and stay at the
            // *same visual column* if possible.
            // So, first convert current cx to rx, then convert that rx to new cx on target line.
            int currentRx = cxToRx(cursorY, cursorX);
            cursorY--;
            if (cursorY < lines.size()) {
                cursorX = rxToCx(cursorY, currentRx); // Find the character index closest to the current rendered column
            }
            else {
                cursorX = 0; // If somehow moved past valid lines, reset
            }
        }
        break;
    case ARROW_DOWN:
        if (cursorY < lines.size() - 1) {
            // Same logic as ARROW_UP
            int currentRx = cxToRx(cursorY, cursorX);
            cursorY++;
            if (cursorY < lines.size()) {
                cursorX = rxToCx(cursorY, currentRx); // Find the character index closest to the current rendered column
            }
            else {
                cursorX = 0;
            }
        }
        break;
    case HOME_KEY:
        cursorX = 0; // Always character index 0
        break;
    case END_KEY:
        if (cursorY < lines.size()) {
            cursorX = lines[cursorY].length(); // Always end of character string
        }
        break;
    case PAGE_UP:
    case PAGE_DOWN: {
        int currentRx = cxToRx(cursorY, cursorX); // Store current rendered X before jump
        int times = screenRows - 2;
        if (key == PAGE_UP) {
            cursorY = std::max(0, cursorY - times);
        }
        else { // PAGE_DOWN
            cursorY = std::min((int)lines.size() - 1, cursorY + times);
        }
        // After changing Y, re-calculate X based on old rendered position
        if (cursorY < lines.size()) {
            cursorX = rxToCx(cursorY, currentRx);
        }
        else if (lines.empty()) { // Edge case: empty file, moved page
            cursorX = 0;
            cursorY = 0;
        }
        else { // Landed on an invalid line, default to end of last line
            cursorY = lines.size() - 1;
            cursorX = lines[cursorY].length();
        }
        break;
    }
    }
}

void Editor::insertChar(int c) {
    if (cursorY == lines.size()) {
        lines.push_back("");
    }
    lines[cursorY].insert(cursorX, 1, static_cast<char>(c));
    cursorX++;
    statusMessage = "";
    statusMessageTime = 0; // Clear immediately
    calculateLineNumberWidth();
    dirty = true;
}

void Editor::insertNewline() {
    if (cursorX == lines[cursorY].length()) {
        lines.insert(lines.begin() + cursorY + 1, "");
    }
    else {
        std::string remaining = lines[cursorY].substr(cursorX);
        lines[cursorY].erase(cursorX);
        lines.insert(lines.begin() + cursorY + 1, remaining);
    }
    cursorY++;
    cursorX = 0;
    calculateLineNumberWidth();
    statusMessage = "";
    statusMessageTime = 0;
    dirty = true;
}

void Editor::deleteChar() { // Backspace
    if (cursorY == lines.size()) return;
    if (cursorX == 0 && cursorY == 0 && lines[0].empty()) {
        return;
    }

    if (cursorX > 0) {
        lines[cursorY].erase(cursorX - 1, 1);
        cursorX--;
    }
    else {
        cursorX = lines[cursorY - 1].length();
        lines[cursorY - 1] += lines[cursorY];
        lines.erase(lines.begin() + cursorY);
        cursorY--;
        calculateLineNumberWidth();
    }
    statusMessage = "";
    statusMessageTime = 0;
    dirty = true;
}

void Editor::deleteForwardChar() { // Delete key
    if (cursorY == lines.size()) return;
    if (cursorX == lines[cursorY].length() && cursorY == lines.size() - 1) {
        return;
    }

    if (cursorX < lines[cursorY].length()) {
        lines[cursorY].erase(cursorX, 1);
    }
    else {
        lines[cursorY] += lines[cursorY + 1];
        lines.erase(lines.begin() + cursorY + 1);
        calculateLineNumberWidth();
    }
    statusMessage = "";
    statusMessageTime = 0;
    dirty = true;
}


bool Editor::openFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        statusMessage = "Error: Could not open file '" + path + "'";
        statusMessageTime = GetTickCount64();
        return false;
    }

    lines.clear();
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    file.close();

    if (lines.empty()) {
        lines.push_back("");
    }

    filename = path;
    cursorX = 0;
    cursorY = 0;
    rowOffset = 0;
    colOffset = 0;
    calculateLineNumberWidth();
    statusMessage = "Opened '" + path + "'";
    statusMessageTime = GetTickCount64();

    // Force a full redraw after file open
    for (auto& s : prevDrawnLines) s.assign(screenCols, ' ');
    prevStatusMessage = "";
    prevMessageBarMessage = "";

    dirty = false;
    return true;
}

bool Editor::saveFile() {
    if (filename.empty()) {
        statusMessage = "Cannot save: No filename specified. Use Ctrl-O to open/create a file.";
        statusMessageTime = GetTickCount64();
        return false;
    }

    std::ofstream file(filename);
    if (!file.is_open()) {
        statusMessage = "Error: Could not save file '" + filename + "'";
        statusMessageTime = GetTickCount64();
        dirty = false;
        return false;
    }

    for (const auto& line : lines) {
        file << line << "\r\n"; // Windows line endings (CRLF)
    }
    file.close();

    statusMessage = "Saved '" + filename + "' (" + std::to_string(lines.size()) + " lines)";
    statusMessageTime = GetTickCount64();
    return true;
}

int Editor::cxToRx(int lineIndex, int cx)
{
    if (lineIndex < 0 || lineIndex >= lines.size()) return 0;

    int rx = 0;
    const std::string& line = lines[lineIndex];
    for (int i = 0; i < cx; ++i) {
        if (line[i] == '\t') {
            rx += (KILO_TAB_STOP - (rx % KILO_TAB_STOP));
        }
        else {
            ++rx;
        }
    }
    return rx;
}

int Editor::rxToCx(int lineIndex, int rx)
{
    if (lineIndex < 0 || lineIndex >= lines.size()) return 0;

    int currentRx = 0;
    int cx = 0;
    const std::string& line = lines[lineIndex];
    for (int i = 0; i < line.length(); ++i) {
        if (line[i] == '\t') {
            currentRx += (KILO_TAB_STOP - (currentRx % KILO_TAB_STOP));
        }
        else {
            currentRx++;
        }
        if (currentRx > rx) return cx;
        ++cx;
    }
    return cx;
}

std::string Editor::getRenderedLine(int fileRow)
{
    if (fileRow < 0 || fileRow >= lines.size()) {
        return "";
    }
    std::string renderedLine;
    const std::string& originalLine = lines[fileRow];
    for (char c : originalLine) {
        if (c == '\t') {
            int spacesToAdd = KILO_TAB_STOP - (renderedLine.length() % KILO_TAB_STOP);
            renderedLine.append(spacesToAdd, ' ');
        }
        else {
            renderedLine += c;
        }
    }
    return renderedLine;
}

void Editor::toggleFileExplorer() {
    if (mode == EDIT_MODE) {
        mode = FILE_EXPLORER_MODE;
        statusMessage = "File Explorer Mode: Navigate with arrows, Enter to open/CD, N for New, D for Delete.";
        statusMessageTime = GetTickCount64();
        populateDirectoryEntries(currentDirPath); // Populate entries when entering mode
        selectedFileIndex = 0; // Reset selection
        fileExporerScrollOffset = 0; // Reset scroll
        clearScreen(); // Force full redraw when changing modes
        for (auto& s : prevDrawnLines) s.assign(screenCols, ' '); // Invalidate text editor view
    }
    else { // FILE_EXPLORER_MODE
        mode = EDIT_MODE;
        statusMessage = "Edit Mode: Ctrl-Q = quit | Ctrl-S = save | Ctrl-O = open | Ctrl-E = explorer";
        statusMessageTime = GetTickCount64();
        directoryEntries.clear(); // Clear entries when exiting
        clearScreen(); // Force full redraw when changing modes
        for (auto& s : prevDrawnLines) s.assign(screenCols, ' '); // Invalidate text editor view
    }
    prevStatusMessage = ""; // Force status bar redraw
    prevMessageBarMessage = ""; // Force message bar redraw
}

void Editor::populateDirectoryEntries(const std::string& path) {
    directoryEntries.clear();

    // Add ".." entry for navigating up
    directoryEntries.push_back({ "..", true });

    WIN32_FIND_DATAA findFileData; // Using A for ANSI string
    HANDLE hFind = FindFirstFileA((path + "\\*").c_str(), &findFileData); // Find files and directories

    if (hFind == INVALID_HANDLE_VALUE) {
        statusMessage = "Error: Could not list directory '" + path + "'";
        statusMessageTime = GetTickCount64();
        return;
    }

    do {
        std::string entryName = findFileData.cFileName;
        // Skip "." and ".." as we handle ".." explicitly
        if (entryName == "." || entryName == "..") {
            continue;
        }

        bool isDir = (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        directoryEntries.push_back({ entryName, isDir });

    } while (FindNextFileA(hFind, &findFileData) != 0);

    FindClose(hFind);

    // Sort entries: directories first, then files, both alphabetically
    std::sort(directoryEntries.begin() + 1, directoryEntries.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.isDirectory && !b.isDirectory) return true;
        if (!a.isDirectory && b.isDirectory) return false;
        return a.name < b.name;
        });

    currentDirPath = path; // Update current path
    statusMessage = "Viewing: " + currentDirPath;
    statusMessageTime = GetTickCount64();
}

void Editor::moveFileExplorerSelection(int key) {
    if (directoryEntries.empty()) return;

    int oldSelectedFileIndex = selectedFileIndex;

    if (key == ARROW_UP) {
        selectedFileIndex = std::max(0, selectedFileIndex - 1);
    }
    else if (key == ARROW_DOWN) {
        selectedFileIndex = std::min((int)directoryEntries.size() - 1, selectedFileIndex + 1);
    }
    else if (key == PAGE_UP) {
        selectedFileIndex = std::max(0, selectedFileIndex - (screenRows - 4)); // -4 for bars and top/bottom padding
    }
    else if (key == PAGE_DOWN) {
        selectedFileIndex = std::min((int)directoryEntries.size() - 1, selectedFileIndex + (screenRows - 4));
    }
    else if (key == HOME_KEY) {
        selectedFileIndex = 0;
    }
    else if (key == END_KEY) {
        selectedFileIndex = directoryEntries.size() - 1;
    }

    // Adjust scroll offset
    if (selectedFileIndex < fileExporerScrollOffset) {
        fileExporerScrollOffset = selectedFileIndex;
    }
    if (selectedFileIndex >= fileExporerScrollOffset + (screenRows - 4)) {
        fileExporerScrollOffset = selectedFileIndex - (screenRows - 4) + 1;
    }

    if (oldSelectedFileIndex != selectedFileIndex) {
        clearScreen();
    }
}

void Editor::handleFileExplorerEnter() {
    if (directoryEntries.empty() || selectedFileIndex < 0 || selectedFileIndex >= directoryEntries.size()) {
        return; // Nothing selected
    }

    DirEntry& selectedEntry = directoryEntries[selectedFileIndex];

    char newPathBuffer[MAX_PATH];
    if (PathCombineA(newPathBuffer, currentDirPath.c_str(), selectedEntry.name.c_str()) == NULL) {
        statusMessage = "Error: Invalid path combination.";
        statusMessageTime = GetTickCount64();
        return;
    }
    std::string newPath = newPathBuffer;

    if (selectedEntry.isDirectory) {
        // Change directory
        populateDirectoryEntries(newPath);
        selectedFileIndex = 0; // Reset selection in new directory
        fileExporerScrollOffset = 0;
        clearScreen(); // Full refresh after CD
    }
    else {
        // Open file in editor mode
        openFile(newPath);
        toggleFileExplorer(); // Switch back to edit mode
    }
}

void Editor::drawFileExplorer() {
    clearScreen(); // Full clear to avoid artifacts when switching from editor

    int startRow = 0; // Starting screen row for explorer list

    // Display current path
    setCursorPosition(0, startRow);
    setTextColor(CYAN | INTENSITY);
    writeStringAt(0, startRow, "PATH: " + currentDirPath);
    resetTextColor();

    startRow += 2; // Leave a line for separation

    int visibleRows = screenRows - startRow - 2; // Remaining rows for list, excluding status/message bars

    for (int i = 0; i < visibleRows; ++i) {
        setCursorPosition(0, startRow + i);
        int entryIndex = fileExporerScrollOffset + i;

        if (entryIndex < directoryEntries.size()) {
            const DirEntry& entry = directoryEntries[entryIndex];
            std::string lineToDraw;

            // Highlight selected item
            if (entryIndex == selectedFileIndex) {  
                setTextColor(BG_BLUE | WHITE | INTENSITY);
                lineToDraw += "> ";
            }
            else {
                setTextColor(WHITE);
                lineToDraw += "  ";
            }

            // Indicate directory or file
            if (entry.isDirectory) {
                setTextColor(BLUE | INTENSITY);
                lineToDraw += "[" + entry.name + "]";
            }
            else {
                setTextColor(WHITE);
                lineToDraw += " " + entry.name;
            }

            std::string formattedEntryText;
            if (entry.isDirectory) {
                formattedEntryText = "[" + entry.name + "]";
            }
            else {
                formattedEntryText = " " + entry.name;
            }

            std::string lineForDraw = (entryIndex == selectedFileIndex ? "> " : "  ") + formattedEntryText;
            lineForDraw += std::string(screenCols - lineForDraw.length(), ' '); // Pad to full width

            // Re-apply colors for the entire line to ensure consistent background
            if (entryIndex == selectedFileIndex) {
                setTextColor(BG_BLUE | WHITE | INTENSITY);
            }
            else if (entry.isDirectory) {
                setTextColor(BLUE | INTENSITY); // Directories, non-selected
            }
            else {
                setTextColor(WHITE); // Files, non-selected
            }

            writeStringAt(0, startRow + i, lineForDraw);
            resetTextColor(); // Reset for next line/outside this loop
        }
        else {
            // Draw empty lines beyond content with default colors
            setCursorPosition(0, startRow + i);
            writeStringAt(0, startRow + i, std::string(screenCols, ' '));
        }
    }
}

void Editor::refreshScreen() {
    updateScreenSize();

    if (mode == EDIT_MODE || mode == PROMPT_MODE) { // PROMPT_MODE also uses the editor display
        scroll();
        drawScreenContent();
    } else if (mode == FILE_EXPLORER_MODE) {
        drawFileExplorer();
    }

    drawStatusBar();
    drawMessageBar();

    if (mode == EDIT_MODE) {
        int finalRenderedCursorX = lineNumberWidth + (cxToRx(cursorY, cursorX) - colOffset);
        int finalRenderedCursorY = cursorY - rowOffset;
        setCursorPosition(finalRenderedCursorX, finalRenderedCursorY);
    } else if (mode == PROMPT_MODE) {
        // Place cursor at the end of the prompt in the message bar
        setCursorPosition(promptMessage.length() + searchQuery.length(), screenRows - 1);
    }
    else { 
        setCursorPosition(0, (selectedFileIndex - fileExporerScrollOffset) + (screenRows - 2));
    }
}