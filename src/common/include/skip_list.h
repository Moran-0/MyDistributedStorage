#pragma once
#include <cstdlib>
#include <cstring>
#include <vector>
#include <mutex>
#include <string>
#include <random>
#include <iostream>
#include <sstream>
#include <shared_mutex>
#include <boost/serialization/access.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/vector.hpp>

constexpr int kInvalidLevel = -1; // 无效层级

// #include <boost/>
/// @brief 跳表节点声明定义
/// @tparam K
/// @tparam V
template <typename K, typename V>
class Node {
  public:
    Node() = default;
    // 禁止移动和拷贝
    Node(const Node&) = delete;
    Node(Node&&) = delete;
    Node& operator=(const Node&) = delete;
    Node& operator=(Node&&) = delete;
    Node(K key, V val, int level);
    ~Node() = default;
    K Key() const {
        return key_;
    }
    V Value() const {
        return value_;
    }
    int Level() const {
        return level_;
    }
    void SetValue(const V& val) {
        value_ = val;
    }
    // 线性数组，存储当前层级及以下的不同层级的下一节点指针
    std::vector<Node*> forward_;
    // Node** forward_;

  private:
    K key_;
    V value_;
    int level_; // 跳表层级
};

template <typename K, typename V>
Node<K, V>::Node(const K key, const V val, int level) {
    key_ = key;
    value_ = val;
    level_ = level;
    forward_ = std::vector<Node*>(level + 1, nullptr);
}

/// @brief 跳表数据转存类
/// @tparam K
/// @tparam V
template <typename K, typename V>
class SkipListDump {
  public:
    friend class boost::serialization::access;
    template <class Archive>
    void serialize(Archive& ar, const unsigned int version) {
        ar & key_dump_;
        ar & value_dump_;
    }
    void Insert(const Node<K, V>& node);

    std::vector<K> key_dump_;
    std::vector<V> value_dump_;
};

template <typename K, typename V>
void SkipListDump<K, V>::Insert(const Node<K, V>& node) {
    key_dump_.push_back(node.Key());
    value_dump_.push_back(node.Value());
}

template <typename K, typename V>
struct SkipListEntry {
    friend class boost::serialization::access;
    K key;
    V val;
    int level;
    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar & key;
        ar & val;
        ar & level;
    }
};

template <typename K, typename V>
struct SkipListSnapshot {
    friend class boost::serialization::access;
    int max_level;
    int cur_level;
    int size;
    std::vector<SkipListEntry<K, V>> entries;
    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar & max_level;
        ar & cur_level;
        ar & size;
        ar & entries;
    }
};

/// @brief 跳表数据结构声明定义
/// @tparam K
/// @tparam V
template <typename K, typename V>
class SkipList {
  public:
    SkipList(int max_level);
    SkipList(const SkipList&) = delete;
    ~SkipList();
    bool Insert(const K& key, const V& value, int level = kInvalidLevel);
    bool Delete(const K& key);
    bool Search(const K& key, V* value);
    bool Update(const K& key, const V& value);
    void Clear();
    void Display() const;

    std::string Dump();
    void Load(const std::string& dump);
    int Size() const {
        std::shared_lock lock(mtx_);
        return size_;
    }

  private:
    bool InsertUnlocked(const K& key, const V& value, int level = kInvalidLevel);
    bool DeleteUnlocked(const K& key);
    bool SearchUnlocked(const K& key, V* value);
    bool UpdateUnlocked(const K& key, const V& value);
    void ClearUnlocked();
    int RandomLevel();
    Node<K, V>* CreateNode(const K& key, const V& value, int level);

  private:
    Node<K, V>* header_;
    int max_level_;
    int current_level_;
    int size_;
    mutable std::shared_mutex mtx_;              // 使用读写锁，支持读并发
    std::mt19937 rng_{std::random_device{}()};   // 随机数生成器
    std::bernoulli_distribution coin_flip_{0.5}; // 50%概率的伯努利分布
};

template <typename K, typename V>
SkipList<K, V>::SkipList(int max_level) : max_level_(max_level), current_level_(0), size_(0) {
    header_ = new Node<K, V>(K(), V(), max_level_);
}

template <typename K, typename V>
SkipList<K, V>::~SkipList() {
    Node<K, V>* current = header_->forward_[0];
    while (current != nullptr) {
        Node<K, V>* next = current->forward_[0];
        delete current;
        current = next;
    }
    delete header_;
}

template <typename K, typename V>
bool SkipList<K, V>::Insert(const K& key, const V& value, int level) {
    std::unique_lock lock(mtx_);
    return InsertUnlocked(key, value, level);
}

template <typename K, typename V>
bool SkipList<K, V>::Delete(const K& key) {
    std::unique_lock lock(mtx_);
    return DeleteUnlocked(key);
}

template <typename K, typename V>
bool SkipList<K, V>::Search(const K& key, V* value) {
    std::shared_lock lock(mtx_);
    return SearchUnlocked(key, value);
}

template <typename K, typename V>
bool SkipList<K, V>::Update(const K& key, const V& value) {
    std::unique_lock lock(mtx_);
    return UpdateUnlocked(key, value);
}

template <typename K, typename V>
void SkipList<K, V>::Clear() {
    std::unique_lock lock(mtx_);
    ClearUnlocked();
}

template <typename K, typename V>
void SkipList<K, V>::Display() const {
    std::shared_lock lock(mtx_);
    for (int i = 0; i <= current_level_; ++i) {
        Node<K, V>* node = header_->forward_[i];
        std::cout << "level " << i << " ";
        while (node != nullptr) {
            std::cout << node->Key() << ":" << node->Value() << " -> ";
            node = node->forward_[i];
        }
        std::cout << '\n';
    }
}

/// @brief 插入一个新节点
/// @tparam K
/// @tparam V
/// @param key
/// @param value
/// @return
template <typename K, typename V>
bool SkipList<K, V>::InsertUnlocked(const K& key, const V& value, int level) {
    std::vector<Node<K, V>*> update(max_level_ + 1, nullptr); // 记录每层要更新的节点
    Node<K, V>* current = header_;
    // 遍历每层，找到最后一个小于key的节点
    for (int i = current_level_; i >= 0; --i) {
        while (current->forward_[i] != nullptr && current->forward_[i]->Key() < key) {
            current = current->forward_[i]; // 在当前层前进到下一节点
        }
        update[i] = current;
    }
    // 遍历结束，处于第0层
    current = current->forward_[0];
    if (current != nullptr && current->Key() == key) {
        // 节点已存在
        return false;
    }
    int node_level = level;
    if (node_level == kInvalidLevel) {
        node_level = RandomLevel();
    }
    // 如果node_level > current_level_，则更新当前层级数据
    if (node_level > current_level_) {
        for (int i = current_level_ + 1; i <= node_level; ++i) {
            update[i] = header_;
        }
        current_level_ = node_level;
    }

    Node<K, V>* new_node = CreateNode(key, value, node_level);
    for (int i = 0; i <= node_level; ++i) {
        new_node->forward_[i] = update[i]->forward_[i];
        update[i]->forward_[i] = new_node;
    }
    ++size_;
    return true;
}

/// @brief 删除节点
/// @tparam K
/// @tparam V
/// @param key
/// @return
template <typename K, typename V>
bool SkipList<K, V>::DeleteUnlocked(const K& key) {
    std::vector<Node<K, V>*> update(max_level_ + 1, nullptr);
    Node<K, V>* current = header_;
    for (int i = current_level_; i >= 0; --i) {
        while (current->forward_[i] != nullptr && current->forward_[i]->Key() < key) {
            current = current->forward_[i];
        }
        update[i] = current;
    }
    current = current->forward_[0];
    // 判断跳表中是否存在该节点key
    if (current != nullptr && current->Key() == key) {
        for (int i = 0; i <= current_level_; ++i) {
            if (update[i]->forward_[i] != current) {
                break;
            }
            update[i]->forward_[i] = current->forward_[i];
        }
        // 如果删除后某个层级没有任何节点了，删除该层级
        while (current_level_ > 0 && header_->forward_[current_level_] == nullptr) {
            --current_level_;
        }
        delete current;
        --size_;
        return true;
    }
    return false;
}

template <typename K, typename V>
bool SkipList<K, V>::SearchUnlocked(const K& key, V* value) {
    if (value == nullptr) {
        return false;
    }
    Node<K, V>* current = header_;

    for (int i = current_level_; i >= 0; --i) {
        while (current->forward_[i] != nullptr && current->forward_[i]->Key() < key) {
            current = current->forward_[i];
        }
    }
    current = current->forward_[0];
    if (current != nullptr && current->Key() == key) {
        *value = current->Value();
        return true;
    }
    return false;
}

template <typename K, typename V>
bool SkipList<K, V>::UpdateUnlocked(const K& key, const V& value) {
    Node<K, V>* current = header_;

    for (int i = current_level_; i >= 0; --i) {
        while (current->forward_[i] != nullptr && current->forward_[i]->Key() < key) {
            current = current->forward_[i];
        }
    }
    current = current->forward_[0];
    if (current != nullptr && current->Key() == key) {
        current->SetValue(value);
        return true;
    }
    return false;
}

template <typename K, typename V>
void SkipList<K, V>::ClearUnlocked() {
    Node<K, V>* cur = header_->forward_[0];
    while (cur != nullptr) {
        Node<K, V>* next = cur->forward_[0];
        delete cur;
        cur = next;
    }
    std::fill(header_->forward_.begin(), header_->forward_.end(), nullptr);
    current_level_ = 0;
    size_ = 0;
}

template <typename K, typename V>
std::string SkipList<K, V>::Dump() {
    std::shared_lock lock(mtx_);
    Node<K, V>* node = header_->forward_[0];
    SkipListSnapshot<K, V> dumper;
    while (node != nullptr) {
        dumper.entries.emplace_back(node->Key(), node->Value(), node->Level());
        node = node->forward_[0];
    }
    dumper.cur_level = current_level_;
    dumper.size = size_;
    dumper.max_level = max_level_;
    lock.unlock();
    std::stringstream ss;
    boost::archive::text_oarchive oa(ss);
    oa << dumper;
    return ss.str();
}

template <typename K, typename V>
void SkipList<K, V>::Load(const std::string& dump) {
    if (dump.empty()) {
        return;
    }
    std::stringstream iss(dump);
    SkipListSnapshot<K, V> dumper;
    boost::archive::text_iarchive oi(iss);
    oi >> dumper;
    ClearUnlocked(); // 清空跳表
    std::unique_lock lock(mtx_);
    max_level_ = dumper.max_level;
    current_level_ = dumper.cur_level;
    size_ = 0;
    for (const SkipListEntry<K, V>& entry : dumper.entries) {
        InsertUnlocked(entry.key, entry.val, entry.level);
    }
    if (size_ != dumper.size) {
        std::cerr << "序列化恢复出错\n";
    }
}

template <typename K, typename V>
Node<K, V>* SkipList<K, V>::CreateNode(const K& key, const V& value, int level) {
    return new Node<K, V>(key, value, level);
}

/// @brief 生成一个随机层级作为新插入节点的层级
/// @tparam K
/// @tparam V
/// @return
template <typename K, typename V>
int SkipList<K, V>::RandomLevel() {
    int level = 0;
    while (coin_flip_(rng_) && level < max_level_) {
        ++level;
    }
    return level;
}
