#pragma once

#include <string>
#include <vector>
#include <memory>
#include <algorithm>

const size_t ROPE_LEAF_CHUNK_SIZE = 256;

struct RopeNode {
    std::string data;
    size_t weight;

    std::unique_ptr<RopeNode> left;
    std::unique_ptr<RopeNode> right;

    RopeNode(const std::string& text) : data(text), weight(text.length()), left(nullptr), right(nullptr) {}

    RopeNode(std::unique_ptr<RopeNode> l, std::unique_ptr<RopeNode> r)
        : data(""), weight((l ? l->weight : 0) + (r ? r->weight : 0)),
          left(std::move(l)), right(std::move(r)) {}

    RopeNode(RopeNode&& other) noexcept = default;
    RopeNode& operator=(RopeNode&& other) noexcept = default;

    bool isLeaf() const { return !left && !right; }
};

class Rope {
public:
    Rope();
    explicit Rope(const std::string& text);
    Rope(const Rope& other);
    Rope& operator=(const Rope& other);
    Rope(Rope&& other) noexcept = default;
    Rope& operator=(Rope&& other) noexcept = default;
    ~Rope() = default;

    size_t length() const;
    size_t lineCount() const;
    std::string toString() const;

    char charAt(size_t index) const;
    std::string substring(size_t start, size_t len) const;

    void insert(size_t index, const std::string& text);
    void remove(size_t index, size_t len);

    size_t getLineStartIndex(size_t lineNumber) const;
    std::string getLine(size_t lineNumber) const;

private:
    std::unique_ptr<RopeNode> root;
    size_t _length;
    size_t _lineCount;

    std::unique_ptr<RopeNode> createNode(const std::string& text);
    std::unique_ptr<RopeNode> createNodeRecursive(const std::string& text, size_t start, size_t end);
    std::unique_ptr<RopeNode> concatenate(std::unique_ptr<RopeNode> left, std::unique_ptr<RopeNode> right);
    std::pair<std::unique_ptr<RopeNode>, std::unique_ptr<RopeNode>> split(std::unique_ptr<RopeNode> node, size_t index);
    
    char charAtRecursive(const RopeNode* node, size_t index) const;
    void substringRecursive(const RopeNode* node, size_t start, size_t len, std::string& result) const;
    
    std::unique_ptr<RopeNode> cloneRecursive(const RopeNode* node) const;
    void updateNodeWeight(RopeNode* node);

    size_t countNewlines(const RopeNode* node) const;
    size_t findNewlineIndex(const RopeNode* node, size_t n_th_newline) const;
};