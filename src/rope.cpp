#include "rope.h"
#include <stack>
#include <stdexcept>

static size_t countNewlinesInString(const std::string& s) {
    size_t count = 0;
    for (char c : s) {
        if (c == '\n') {
            count++;
        }
    }
    return count;
}

Rope::Rope() : root(nullptr), _length(0), _lineCount(1) {}

Rope::Rope(const std::string& text) : _length(0), _lineCount(0) {
    if (text.empty()) {
        root = nullptr;
        _length = 0;
        _lineCount = 1;
    } else {
        root = createNode(text);
        _length = root->weight;
        _lineCount = countNewlines(root.get()) + 1;
    }
}

Rope::Rope(const Rope& other) : _length(other._length), _lineCount(other._lineCount) {
    if (other.root) {
        root = cloneRecursive(other.root.get());
    } else {
        root = nullptr;
    }
}

Rope& Rope::operator=(const Rope& other) {
    if (this != &other) {
        _length = other._length;
        _lineCount = other._lineCount;
        if (other.root) {
            root = cloneRecursive(other.root.get());
        } else {
            root = nullptr;
        }
    }
    return *this;
}

size_t Rope::length() const {
    return _length;
}

size_t Rope::lineCount() const {
    return _lineCount;
}

std::string Rope::toString() const {
    std::string result = "";
    if (!root) {
        return result;
    }
    std::stack<const RopeNode*> s;
    s.push(root.get());

    while (!s.empty()) {
        const RopeNode* current = s.top();
        s.pop();

        if (current->isLeaf()) {
            result += current->data;
        } else {
            if (current->right) s.push(current->right.get());
            if (current->left) s.push(current->left.get());
        }
    }
    return result;
}

char Rope::charAt(size_t index) const {
    if (index >= _length) {
        throw std::out_of_range("Rope::charAt: index out of bounds.");
    }
    return charAtRecursive(root.get(), index);
}

std::string Rope::substring(size_t start, size_t len) const {
    if (start >= _length || len == 0) {
        return "";
    }
    if (start + len > _length) {
        len = _length - start;
    }
    std::string result;
    substringRecursive(root.get(), start, len, result);
    return result;
}

void Rope::insert(size_t index, const std::string& text) {
    if (index > _length) {
        index = _length;
    }
    if (text.empty()) {
        return;
    }

    std::unique_ptr<RopeNode> newTextNode = createNode(text);
    
    if (!root) {
        root = std::move(newTextNode);
    } else {
        std::pair<std::unique_ptr<RopeNode>, std::unique_ptr<RopeNode>> parts = split(std::move(root), index);
        std::unique_ptr<RopeNode> left_part = std::move(parts.first);
        std::unique_ptr<RopeNode> right_part = std::move(parts.second);
        root = concatenate(concatenate(std::move(left_part), std::move(newTextNode)), std::move(right_part));
    }
    _length += text.length();
    _lineCount += countNewlinesInString(text);
}

void Rope::remove(size_t index, size_t len) {
    if  (_length == 0) {
      _lineCount = 1;
      root = nullptr;
      return;
    }
    if (index >= _length || len == 0) {
        return;
    }
    if (index + len > _length) {
        len = _length - index;
    }

    std::string removed_content = substring(index, len);

    std::pair<std::unique_ptr<RopeNode>, std::unique_ptr<RopeNode>> parts_after_removal = split(std::move(root), index + len);
    std::unique_ptr<RopeNode> temp_left = std::move(parts_after_removal.first);
    std::unique_ptr<RopeNode> temp_removed_part = std::move(parts_after_removal.second);

    std::pair<std::unique_ptr<RopeNode>, std::unique_ptr<RopeNode>> final_split = split(std::move(temp_left), index);
    std::unique_ptr<RopeNode> kept_left = std::move(final_split.first);
    std::unique_ptr<RopeNode> removed_part_actual = std::move(final_split.second);
    
    root = std::move(kept_left);
    
    _length -= len;
    _lineCount -= countNewlinesInString(removed_content);
    if (_length == 0) {
        _lineCount = 1;
        root = nullptr; 
    }
}

size_t Rope::getLineStartIndex(size_t lineNumber) const {
    if (lineNumber == 0) {
        return 0;
    }
    if (lineNumber >= _lineCount) {
        return _length;
    }
    size_t newline_char_index = findNewlineIndex(root.get(), lineNumber);
    if (newline_char_index == (size_t)-1) {
        return _length;
    }
    return newline_char_index + 1;
}

std::string Rope::getLine(size_t lineNumber) const {
    if (lineNumber >= _lineCount) {
        return "";
    }
    size_t start_char_index = getLineStartIndex(lineNumber);
    size_t end_char_index = _length;

    size_t current_char_index = start_char_index;
    while (current_char_index < _length) {
        char c = charAt(current_char_index);
        if (c == '\n') {
            end_char_index = current_char_index;
            break;
        }
        current_char_index++;
    }
    return substring(start_char_index, end_char_index - start_char_index);
}

std::unique_ptr<RopeNode> Rope::createNode(const std::string& text) {
    if (text.empty()) return nullptr;
    return createNodeRecursive(text, 0, text.length());
}

std::unique_ptr<RopeNode> Rope::createNodeRecursive(const std::string& text, size_t start, size_t end) {
    size_t length = end - start;
    if (length == 0) return nullptr;

    if (length <= ROPE_LEAF_CHUNK_SIZE) {
        return std::make_unique<RopeNode>(text.substr(start, length));
    }
    else {
        size_t mid_char_idx = start + length / 2;
        auto left_node = createNodeRecursive(text, start, mid_char_idx);
        auto right_node = createNodeRecursive(text, mid_char_idx, end);
        if (!left_node && !right_node) return nullptr;
        if (!left_node) return right_node;
        if (!right_node) return left_node;
        return concatenate(std::move(left_node), std::move(right_node));
    }
}

std::unique_ptr<RopeNode> Rope::concatenate(std::unique_ptr<RopeNode> left, std::unique_ptr<RopeNode> right) {
    if (!left) return right;
    if (!right) return left;
    auto new_node = std::make_unique<RopeNode>(std::move(left), std::move(right));
    updateNodeWeight(new_node.get());
    return new_node;
}

std::pair<std::unique_ptr<RopeNode>, std::unique_ptr<RopeNode>> Rope::split(std::unique_ptr<RopeNode> node, size_t index) {
    if (!node) {
        return { nullptr, nullptr };
    }
    if (index == 0) {
        return { nullptr, std::move(node) };
    }
    if (index >= node->weight) {
        return { std::move(node), nullptr };
    }

    if (node->isLeaf()) {
        auto left_str = node->data.substr(0, index);
        auto right_str = node->data.substr(index);
        return { std::make_unique<RopeNode>(left_str), std::make_unique<RopeNode>(right_str) };
    }
    else {
        if (index <= node->left->weight) {
            std::pair<std::unique_ptr<RopeNode>, std::unique_ptr<RopeNode>> split_left_result = split(std::move(node->left), index);
            std::unique_ptr<RopeNode> left_part = std::move(split_left_result.first);
            std::unique_ptr<RopeNode> right_part = std::move(split_left_result.second);
            std::unique_ptr<RopeNode> new_right = concatenate(std::move(right_part), std::move(node->right));
            return { std::move(left_part), std::move(new_right) };
        }
        else {
            size_t adjusted_index = index - node->left->weight;
            std::pair<std::unique_ptr<RopeNode>, std::unique_ptr<RopeNode>> split_right_result = split(std::move(node->right), adjusted_index);
            std::unique_ptr<RopeNode> left_part = std::move(split_right_result.first);
            std::unique_ptr<RopeNode> right_part = std::move(split_right_result.second);
            std::unique_ptr<RopeNode> new_left = concatenate(std::move(node->left), std::move(left_part));
            return { std::move(new_left), std::move(right_part) };
        }
    }
}

char Rope::charAtRecursive(const RopeNode* node, size_t index) const {
    if (node->isLeaf()) {
      if (index >= node->data.length()) {
            throw std::logic_error("charAtRecursive: Index out of bounds for leaf node data.");
        }
        return node->data[index];
    } else {
        if (index < node->left->weight) {
            return charAtRecursive(node->left.get(), index);
        } else {
            return charAtRecursive(node->right.get(), index - node->left->weight);
        }
    }
}

void Rope::substringRecursive(const RopeNode* node, size_t start, size_t len, std::string& result) const {
    if (!node || len == 0 || start >= node->weight) return;

    size_t node_total_length = node->weight;
    if (start >= node_total_length) return;

    if (start + len > node_total_length) {
        len = node_total_length - start;
    }

    if (node->isLeaf()) {
        result += node->data.substr(start, len);
    } else {
        if (start < node->left->weight) {
            substringRecursive(node->left.get(), start, std::min(len, node->left->weight - start), result);
        }
        if (start + len > node->left->weight) {
            size_t right_child_start = std::max((size_t)0, start - node->left->weight);
            size_t right_child_len = len - (node->left->weight > start ? node->left->weight - start : 0);
            if (node->right) {
                substringRecursive(node->right.get(), right_child_start, right_child_len, result);
            }
        }
    }
}

std::unique_ptr<RopeNode> Rope::cloneRecursive(const RopeNode* node) const {
    if (!node) return nullptr;
    if (node->isLeaf()) {
        return std::make_unique<RopeNode>(node->data);
    }
    else {
        return std::make_unique<RopeNode>(cloneRecursive(node->left.get()), cloneRecursive(node->right.get()));
    }
}

void Rope::updateNodeWeight(RopeNode* node) {
    if (!node) return;
    if (node->isLeaf()) {
        node->weight = node->data.length();
    } else {
        node->weight = (node->left ? node->left->weight : 0) + (node->right ? node->right->weight : 0);
    }
}

size_t Rope::countNewlines(const RopeNode* node) const {
    if (!node) return 0;
    if (node->isLeaf()) {
        return countNewlinesInString(node->data);
    } else {
        return countNewlines(node->left.get()) + countNewlines(node->right.get());
    }
}

size_t Rope::findNewlineIndex(const RopeNode* node, size_t n_th_newline) const {
    if (!node) return (size_t)-1;

    if (node->isLeaf()) {
        size_t found_count = 0;
        size_t current_offset = 0;
        for (char c : node->data) {
            if (c == '\n') {
                found_count++;
                if (found_count == n_th_newline) {
                    return current_offset;
                }
            }
            current_offset++;
        }
        return (size_t)-1;
    } else {
        size_t left_newlines = countNewlines(node->left.get());
        if (n_th_newline <= left_newlines) {
            return findNewlineIndex(node->left.get(), n_th_newline);
        } else {
            size_t right_index = findNewlineIndex(node->right.get(), n_th_newline - left_newlines);
            if (right_index == (size_t)-1) return (size_t)-1;
            return node->left->weight + right_index;
        }
    }
}