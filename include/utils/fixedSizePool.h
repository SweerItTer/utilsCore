/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-11-03 16:47:57
 * @FilePath: /EdgeVision/include/utils/fixedSizePool.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include <atomic>
#include <vector>
#include <mutex>

class FixedSizePool {
public:
	FixedSizePool(std::size_t blockSize, std::size_t blocksPerPage = 1024) {
		std::size_t minSize = sizeof(void*);
		if (blockSize < minSize) {
			blockSize = minSize;
		}
		blockSize_ = align_up(blockSize, alignof(void*));
		blocksPerPage_ = blocksPerPage;
	}
	~FixedSizePool() {
		for (auto& p : pages_) {
			::operator delete[] (p);
		}
	}

    void* allocate() {
        Node *headNode = nullptr;
        while (true){
            // 检查头节点有效性
            headNode = freelListHeadptr.load(std::memory_order_acquire);
            if (nullptr == headNode) {
                expand(); // 内存耗尽时扩容一页
                continue;
            }
            // 获取后一块指针
            Node* next = headNode->nextNode;
            // 将头节点往后移动( freelListHeadptr 从期待的 headNode 移动到 headNode->next)
            if (freelListHeadptr.compare_exchange_weak(headNode, next,
                    std::memory_order_release, std::memory_order_acquire)) {
                // 修改成功后返回旧头节点Node*  备份,非原子变量
                return headNode;
            }
            // CAS 失败 node 已被更新, 重新读取
        }
    }
    
	void deallocate(void* p) {
		if (nullptr == p) return; // 无效内存
		Node* node = reinterpret_cast<Node*>(p);	// 将归还的p作为Node
		// 头插法
        Node* old_head = freelListHeadptr.load(std::memory_order_relaxed);
        do {
            // 将Node作为头节点, 下一节点指向原头节点
            node->nextNode = old_head;
        } while (!freelListHeadptr.compare_exchange_weak(
            old_head, node, // 更新头节点指针
            std::memory_order_release,
            std::memory_order_relaxed
        ));		
	}
private:
	std::size_t align_up(std::size_t rawSize, std::size_t align) {
		return (rawSize + (align - 1)) & ~(align - 1);
	}

	void expand() {
        std::lock_guard<std::mutex> lock(expandMtx);
        if (freelListHeadptr.load(std::memory_order_acquire) != nullptr) return; // 已经有人扩容了
		
        auto pageSize = blockSize_ * blocksPerPage_;    // 分配一页的大小
		char* page = reinterpret_cast<char*>(::operator new[](pageSize));
		pages_.push_back(page);

		for (std::size_t i = 0; i < blocksPerPage_; i++) {
			char* addr = page + i * blockSize_; // 从page开始偏移 blockSize_ 作为一个分隔点
			Node* node = reinterpret_cast<Node*>(addr);
            Node* old_head = freelListHeadptr.load(std::memory_order_relaxed);
            do {
                node->nextNode = old_head;  // 将 node 作为头节点, 下一阶段指向原头节点
            } while (!freelListHeadptr.compare_exchange_weak(
                old_head, node, // 更新头节点指针
                std::memory_order_release,
                std::memory_order_relaxed
            ));		
		}
	}
private:
	struct Node {
		Node* nextNode = nullptr;
	}; 
	
	std::atomic<Node*> freelListHeadptr{nullptr}; // 原子头节点

	std::size_t blockSize_;	// 单个块的大小
	std::size_t blocksPerPage_;	// 单分页的块个数
	std::vector<void*> pages_;	// 页
    std::mutex expandMtx;       // 分配页时需要避免多线程扩容
};