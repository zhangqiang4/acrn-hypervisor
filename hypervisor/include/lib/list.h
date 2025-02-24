/*-
 * Copyright (C) 2005-2011 HighPoint Technologies, Inc.
 * Copyright (c) 2017-2025 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef LIST_H_
#define LIST_H_

#include <types.h>


/**
 * @addtogroup lib_util
 *
 * @{
 */

/**
 * @file
 * @brief Implementation of list and hash list function.
 *
 * This file implements APIs to support list operation.
 * APIs to support hash list operation are also included.
 */

/**
 * @brief Data structure to define a linked list node
 *
 * It is intended to be used to link the next and previous node of the linked list.
 */
struct list_head {
	struct list_head *next; /**< Pointer to the next node in the list. */
	struct list_head *prev; /**< Pointer to the previous node in the list. */
};

/**
 * @brief Data structure to define a head node of hash list
 *
 * It is intended to be used to represent the head of a hash list.
 */
struct hlist_head {
	struct hlist_node *first; /**< Pointer to the first node in the linked list. */
};

/**
 * @brief Data structure for a node of hash list
 *
 * It is intended to be used to represent a node of a hash list.
 */
struct hlist_node {
	struct hlist_node *next; /**< Pointer to the next node in the list. */
	struct hlist_node **pprev; /**< Pointer to the previous node's `next` pointer. */
};

/**
 * @brief Initialize linked head.
 *
 * This macro implements initialization of the linked list by setting next and prev to ptr.
 *
 * @param[inout] ptr       Pointer pointing to list head
 *
 * @return None.
 *
 * @pre ptr != NULL
 *
 * @post N/A
 */
#define INIT_LIST_HEAD(ptr) do { (ptr)->next = (ptr); (ptr)->prev = (ptr); } \
	while (false)

/**
 * @brief Insert a new node to the list.
 *
 * This inline function implement the insert node operation for specific list.
 *
 * - It could have the initial config like this: prev <-> next, after adding new_node between prev
 * and next, the list becomes prev <-> new_node <-> next.
 *
 * @param[inout] new_node     The new node to be added
 * @param[in] prev            The previous node in the list
 * @param[in] next            The next node in the list
 *
 * @return None.
 *
 * @pre new_node != NULL
 * @pre prev != NULL
 * @pre next != NULL
 *
 * @post N/A
 */
static inline void list_add_node(struct list_head *new_node, struct list_head *prev,
	struct list_head *next)
{
	next->prev = new_node;
	new_node->next = next;
	new_node->prev = prev;
	prev->next = new_node;
}

/**
 * @brief Add a new node after specific node of list.
 *
 * This inline function implements the inserting node operation after a specific node for the list.
 *
 * - It calls the function list_add_node() to insert a new node in between the head node of the list
 * and the next node of the head node of the list. For detail operation, refer to function list_add_node().
 *
 * @param[inout] new_node     struct list_head pointer pointing to the new node to add to the list
 * @param[in] head            struct list_head pointer pointing to the previous node of the list
 *
 * @return None.
 *
 * @pre new_node != NULL
 * @pre head != NULL
 * @pre head->next != NULL
 *
 * @post N/A
 */
static inline void list_add(struct list_head *new_node, struct list_head *head)
{
	list_add_node(new_node, head, head->next);
}

/**
 * @brief Add a new node before specific node of list.
 *
 * This inline function implements the inserting node operation before a specific node for the list.
 *
 * - It calls the function list_add_node() to insert a new node in between the head node of the list
 * and the previous node of the head node of the list. For detail operation, refer to function list_add_node().
 *
 * @param[inout] new_node     struct list_head pointer pointing to the new node to add to the list
 * @param[in] head            struct list_head pointer pointing to the previous node of the list
 *
 * @return None.
 *
 * @pre new_node != NULL
 * @pre head != NULL
 * @pre head->prev != NULL
 *
 * @post N/A
 */
static inline void list_add_tail(struct list_head *new_node,
	struct list_head *head)
{
	list_add_node(new_node, head->prev, head);
}

/**
 * @brief Delete a node between two nodes of the list.
 *
 * This inline function implement the deleting node operation between two entries of the list.
 *
 * - Initially, it could be like [prev] <-> [to_be_deletetd] <-> [next]. After the operation, it could
 * be like [prev] <-> [next].
 *
 * @param[inout] prev         struct list_head pointer pointing to the previous node to delete
 * @param[inout] next         struct list_head pointer pointing to the next node to delete
 *
 * @return None.
 *
 * @pre prev != NULL
 * @pre next != NULL
 *
 * @post N/A
 */
static inline void list_del_node(struct list_head *prev, struct list_head *next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * @brief Delete a node entry of the list.
 *
 * This inline function implements the deleting node operation for the list.
 *
 * - It calls the function list_del_node() to delete a entry node of the list. For detail
 * operation, refer to function list_del_node().
 *
 * @param[in] entry          struct list_head pointer pointing to the node to delete
 *
 * @return None.
 *
 * @pre entry != NULL
 * @pre entry->prev != NULL
 * @pre entry->next != NULL
 *
 * @post N/A
 */
static inline void list_del(const struct list_head *entry)
{
	list_del_node(entry->prev, entry->next);
}

/**
 * @brief Delete a node entry of the list and initialize the entry's previous and next node.
 *
 * This inline function implements the deleting node and initializing operation for the entry.
 *
 * - It calls the function list_del_node() to delete a entry node of the list, and then initialize
 * the previous and next node of the entry with the entry itself. For detail operation, refer to
 * function list_del_node().
 *
 * @param[inout] entry          struct list_head pointer pointing to the node to delete
 *
 * @return None.
 *
 * @pre entry != NULL
 * @pre entry->prev != NULL
 * @pre entry->next != NULL
 *
 * @post N/A
 */
static inline void list_del_init(struct list_head *entry)
{
	list_del_node(entry->prev, entry->next);
	INIT_LIST_HEAD(entry);
}

/**
 * @brief Check if a list is empty.
 *
 * This inline function determines if a list is empty by checking whether the next node of the list's head
 * points to itself.
 *
 * - It checks if the head node equals with the next node of the head node, then return 1 if
 * they equal, otherwise return 0.
 *
 * @param[inout] head          Constant struct list_head pointer pointing to the node to check
 *
 * @return A bool type value to indicate the status of the next node the the entry.
 *
 * @retval true  When the head node has no next node.
 * @retval false When the head node has the next node.
 *
 * @pre head != NULL
 *
 * @post N/A
 */
static inline _Bool list_empty(const struct list_head *head)
{
	return head->next == head;
}

/**
 * @brief Splice one doubly linked list to another.
 *
 * This inline function splices two list, adding a list node into an existing list.
 *
 * - It could be like this before operation, the source list could be [list] -> [first] <-> ... <-> [last] -> [list],
 * the target list could be [head] <-> [at], after operation, it could be
 * [head] <-> [first] <-> ... <-> [last] <-> [at].
 *
 * @param[in] list           constant struct list_head pointer pointing to the node to be spliced
 * @param[inout] head        struct list_head pointer pointing to the node to splice with
 *
 * @return None.
 *
 * @pre list != NULL
 * @pre head != NULL
 * @pre list->next != NULL
 * @pre list->prev != NULL
 * @pre head->next != NULL
 *
 * @post N/A
 */
static inline void list_splice_node(const struct list_head *list,
				 struct list_head *head)
{
	struct list_head *first = list->next;
	struct list_head *last = list->prev;
	struct list_head *at = head->next;

	first->prev = head;
	head->next = first;

	last->next = at;
	at->prev = last;
}

/**
 * @brief Splice one non empty list to another list.
 *
 * This inline function splices two list, adding a non empty list node into an existing list.
 *
 * - It calls the function list_empty() to check if the list is empty, if it is not, then calls
 * function list_splice_node() to do the splicing for list and head, otherwise, do nothing.
 *
 * @param[in] list           constant struct list_head pointer pointing to the node to be spliced
 * @param[inout] head        struct list_head pointer pointing to the node to splice with
 *
 * @return None.
 *
 * @pre list != NULL
 * @pre head != NULL
 * @pre list->next != NULL
 * @pre list->prev != NULL
 * @pre head->next != NULL
 *
 * @post N/A
 */
static inline void list_splice(const struct list_head *list, struct list_head *head)
{
	if (!list_empty(list)) {
		list_splice_node(list, head);
	}
}

/**
 * @brief Splice one non empty list to another list and initialize the list.
 *
 * This inline function splices two list, adding a non empty list node into an existing list
 * and initialize the first parameter list.
 *
 * - It calls the function list_empty() to check if the list is empty, if it is not, then calls
 * function list_splice_node() to do the splicing for list and head and then initialize the previous
 * and next node of the list with itself, otherwise, do nothing.
 *
 * @param[inout] list        struct list_head pointer pointing to the node to be spliced
 * @param[inout] head        struct list_head pointer pointing to the node to splice with
 *
 * @return None.
 *
 * @pre list != NULL
 * @pre head != NULL
 * @pre list->next != NULL
 * @pre list->prev != NULL
 * @pre head->next != NULL
 *
 * @post N/A
 */
static inline void list_splice_init(struct list_head *list,
	struct list_head *head)
{
	if (!list_empty(list)) {
		list_splice_node(list, head);
		INIT_LIST_HEAD(list);
	}
}

/**
 * @brief Get the address of parent structure.
 *
 * This macro gets the address of a parent structure given the address of one of its member.
 *
 * - It calculates the byte offset of the member within the structure type and subtracts the offset of the member to
 * gets the starting address of the parent structure. and then casts the result back to the type of the containing
 * structure.
 *
 * @param[in] ptr          Pointer pointing to the member
 * @param[in] type         The type of the parent structure
 * @param[in] member       The name of the member in the parent structure
 *
 * @return A pointer corresponding to the given parent structure
 *
 * @pre ptr != NULL
 *
 * @post N/A
 */
#define container_of(ptr, type, member) \
	((type *)(((char *)(ptr)) - offsetof(type, member)))

/**
 * @brief Iterate linked list.
 *
 * This macro iterates over all elements in a linked list. It moves from one node to the next by following the
 * next pointer in the struct list_head, stopping when it loops back to the list head.
 *
 * @param[inout] pos       A pointer used to traverse the list
 * @param[in] head         The head of the list
 *
 * @return None.
 *
 * @pre pos != NULL
 * @pre head != NULL
 *
 * @post N/A
 */
#define list_for_each(pos, head) \
	for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)

/**
 * @brief Iterate linked list safely.
 *
 * This macro safely iterates by preloading the next pointer n before any modifications are made to the current
 * node pos.
 *
 * @param[inout] pos    A pointer used to traverse the list
 * @param[in] n         A pointer to the next node
 * @param[in] head      The head of the list
 *
 * @return None.
 *
 * @pre pos != NULL
 * @pre head != NULL
 *
 * @post N/A
 */
#define list_for_each_safe(pos, n, head) \
	for ((pos) = (head)->next, (n) = (pos)->next; (pos) != (head); \
		(pos) = (n), (n) = (pos)->next)
/**
 * @brief Retrieve the first element of a container structure linked.
 *
 * This macro retrieves a pointer to the structure (type) containing the first struct list_head
 * node in the list. It calculates the container structure's address by offsetting from the next
 * pointer of the list head.
 *
 * @param[in] attached     A pointer for list head
 * @param[in] type         The type of the structure
 * @param[in] member       The name of the member within the type structure
 *
 * @return A pointer corresponding to the head of parent structure
 *
 * @pre pos != NULL
 * @pre head != NULL
 *
 * @post N/A
 */
#define get_first_item(attached, type, member) \
	((type *)((char *)((attached)->next)-(uint64_t)(&((type *)0)->member)))

/**
 * @brief Remove a node from a hash list.
 *
 * This inline function remove a node from a hashed list, and it ensures that the node
 * is properly disconnected from the list without breaking the connections between other nodes.
 *
 * - It could be like this before operation, [prev] -> [n] -> [next], after the operation, it could be
 * like this [prev] -> [next].
 *
 * @param[in] n            struct hlist_node pointer pointing to the node to remove
 *
 * @return None.
 *
 * @pre n != NULL
 *
 * @post N/A
 */
static inline void
hlist_del(struct hlist_node *n)
{

        if (n->next != NULL) {
                n->next->pprev = n->pprev;
	}
        *n->pprev = n->next;
}


/**
 * @brief Add a node to the head of a hashed list.
 *
 * This inline function inserts a new node at the beginning of a hashed list.
 *
 * - It could have the initial state of list like this h->first -> [node1] -> [node2] -> NULL, after the operation,
 * it could be like this h->first -> [new_node] -> [node1] -> [node2] -> NULL.
 *
 * @param[inout] n         struct hlist_node pointer pointing to the new node to add to the list
 * @param[inout] h         struct hlist_head pointer pointing to the head of the list where the node should be inserted
 *
 * @return None.
 *
 * @pre n != NULL
 * @pre h != NULL
 *
 * @post N/A
 */
static inline void
hlist_add_head(struct hlist_node *n, struct hlist_head *h)
{

	n->next = h->first;
	if (h->first != NULL) {
		h->first->pprev = &n->next;
	}
	h->first = n;
	n->pprev = &h->first;
}

/**
 * @brief Wrap macro container_of().
 *
 * This macro gets the address of a parent structure given the address of one of its member,
 * specially used for hash list, refer to macro containe_of() for detail operation.
 *
 * @param[inout] ptr       Pointer pointing to the member
 * @param[in] type         The type of the parent structure
 * @param[in] member       The name of the member in the parent structure
 *
 * @return None.
 *
 * @pre ptr != NULL
 *
 * @post N/A
 */
#define hlist_entry(ptr, type, member) container_of(ptr,type,member)

/**
 * @brief Iterate hash list.
 *
 * This macro iterates over all elements in a hash list. It moves from one node to the next by following the
 * next pointer in the list, stopping when it is NULL.
 *
 * @param[in] pos       A pointer used to traverse the list
 * @param[in] head      The head of hash list
 *
 * @return None.
 *
 * @pre head != NULL
 *
 * @post N/A
 */
#define hlist_for_each(pos, head) \
	for ((pos) = (head)->first; (pos) != NULL; (pos) = (pos)->next)

/**
 * @}
 */

#endif /* LIST_H_ */
