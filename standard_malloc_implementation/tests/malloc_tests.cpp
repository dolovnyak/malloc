#include <gtest/gtest.h>

extern "C" {
#include "malloc_internal.h"
#include "utilities.h"
}

#define TEST_ZONE_SIZE 4096

static BYTE* test_zone = (BYTE*)mmap(0, TEST_ZONE_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE,
                                     VM_MAKE_TAG(VM_MEMORY_MALLOC), 0);
static BYTE* test_fully_occupied_zone = (BYTE*)mmap(0, TEST_ZONE_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE,
                                                    VM_MAKE_TAG(VM_MEMORY_MALLOC), 0);

TEST(Split_Node, Check_Correct) {
    bzero(test_zone, TEST_ZONE_SIZE);
    t_zone* zone = (t_zone*)test_zone;
    BYTE* node = ((BYTE*)zone + 248);

    zone->last_free_node = node;

    set_node_size(node, 128);
    set_previous_node_size(node, 52);
    set_node_zone_start_offset(node, 248);
    set_next_free_node_zone_start_offset(node, 28);
    set_node_available(node, TRUE);
    set_node_allocation_type(node, Small);

    size_t first_node_new_size = 48;
    separate_free_node(node, first_node_new_size, zone);
    BYTE* new_node = node + NODE_HEADER_SIZE + get_node_size(node);

    ASSERT_EQ(get_node_size(node), 48);
    ASSERT_EQ(get_previous_node_size(node), 52);
    ASSERT_EQ(get_node_zone_start_offset(node), 248);
    ASSERT_EQ(get_next_free_node_zone_start_offset(node), 248 + NODE_HEADER_SIZE + 48);
    ASSERT_EQ(get_next_free_node((BYTE*)zone, node), new_node);
    ASSERT_EQ(get_node_available(node), TRUE);
    ASSERT_EQ(get_node_allocation_type(node), Small);

    ASSERT_EQ(get_node_size(new_node), 128 - NODE_HEADER_SIZE - first_node_new_size);
    ASSERT_EQ(get_previous_node_size(new_node), 48);
    ASSERT_EQ(get_node_zone_start_offset(new_node), 248 + NODE_HEADER_SIZE + 48);
    ASSERT_EQ((BYTE*)zone + get_node_zone_start_offset(new_node), new_node);
    ASSERT_EQ(get_next_free_node_zone_start_offset(new_node), 28);
    ASSERT_EQ(get_node_available(new_node), TRUE);
    ASSERT_EQ(get_node_allocation_type(new_node), Small);
}

TEST(Take_Mem_From_Free_Nodes, Empty_List) {
    t_zone* zone = (t_zone*)test_zone;
    bzero(test_zone, TEST_ZONE_SIZE);
    zone->next = nullptr;
    zone->total_size = TEST_ZONE_SIZE;
    zone->first_free_node = nullptr;
    zone->last_free_node = nullptr;
    void* mem = take_memory_from_zone_list(zone, 16, 64, Small);
    ASSERT_EQ(mem, nullptr);
}

TEST(Take_Mem_From_Free_Nodes, Check_Without_Separated) {
    t_zone* occupied_zone = (t_zone*)test_fully_occupied_zone;
    occupied_zone->first_free_node = nullptr;
    occupied_zone->last_free_node = nullptr;
    occupied_zone->total_size = TEST_ZONE_SIZE - ZONE_HEADER_SIZE;

    BYTE* last_allocated_node = (BYTE*)occupied_zone + (occupied_zone->total_size + ZONE_HEADER_SIZE) - NODE_HEADER_SIZE - 16;
    set_node_size(last_allocated_node, 16);
    set_previous_node_size(last_allocated_node, 16);
    set_node_zone_start_offset(last_allocated_node, occupied_zone->total_size - NODE_HEADER_SIZE - 16);
    set_next_free_node(last_allocated_node, 0);
    set_node_available(last_allocated_node, FALSE);
    set_node_allocation_type(last_allocated_node, Tiny);
    occupied_zone->last_allocated_node = last_allocated_node; // there is no space for any other node

    {
        /// check first node
        bzero(test_zone, TEST_ZONE_SIZE);

        BYTE* node1 = test_zone + ZONE_HEADER_SIZE;
        set_node_size(node1, 38);
        set_previous_node_size(node1, 0);
        set_node_zone_start_offset(node1, ZONE_HEADER_SIZE);
        set_node_available(node1, TRUE);
        set_node_allocation_type(node1, Tiny);

        BYTE* node2 = node1 + NODE_HEADER_SIZE + get_node_size(node1);
        set_node_size(node2, 26);
        set_previous_node_size(node2, get_node_size(node1));
        set_node_zone_start_offset(node2, get_node_zone_start_offset(node1) + NODE_HEADER_SIZE + get_node_size(node1));
        set_node_available(node2, TRUE);
        set_node_allocation_type(node2, Tiny);
        set_next_free_node(node1, node2);

        BYTE* node3 = node2 + NODE_HEADER_SIZE + get_node_size(node2);
        set_node_size(node3, 64);
        set_previous_node_size(node3, get_node_size(node2));
        set_node_zone_start_offset(node3, get_node_zone_start_offset(node2) + NODE_HEADER_SIZE + get_node_size(node2));
        set_node_available(node3, TRUE);
        set_node_allocation_type(node3, Tiny);
        set_next_free_node(node2, node3);
        set_next_free_node(node3, nullptr);

        t_zone* zone = (t_zone*)test_zone;
        zone->last_allocated_node = (BYTE*)zone + zone->total_size - NODE_HEADER_SIZE + 16; // there is no space for any other node
        zone->total_size = TEST_ZONE_SIZE - ZONE_HEADER_SIZE;
        zone->first_free_node = node1;
        zone->last_free_node = node3;
        zone->next = nullptr;
        occupied_zone->next = zone;

        void* mem = take_memory_from_zone_list(occupied_zone, 16, 64, Tiny);
        ASSERT_TRUE(mem != nullptr);
        ASSERT_EQ(zone->first_free_node, node2);
        ASSERT_EQ(zone->last_free_node, node3);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, node2), node3);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, node3), nullptr);

        BYTE* node = (BYTE*)zone + ZONE_HEADER_SIZE;
        ASSERT_EQ(node + NODE_HEADER_SIZE, (BYTE*)mem);
        ASSERT_EQ(node, node1);
        ASSERT_EQ(get_node_size(node), 38);
        ASSERT_EQ(get_previous_node_size(node), 0);
        ASSERT_EQ(get_node_zone_start_offset(node), ZONE_HEADER_SIZE);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, node), nullptr);
        ASSERT_EQ(get_node_available(node), FALSE);
        ASSERT_EQ(get_node_allocation_type(node), Tiny);
    }

    {
        /// check last node
        bzero(test_zone, TEST_ZONE_SIZE);

        BYTE* node1 = test_zone + ZONE_HEADER_SIZE;
        set_node_size(node1, 38);
        set_previous_node_size(node1, 0);
        set_node_zone_start_offset(node1, ZONE_HEADER_SIZE);
        set_node_available(node1, TRUE);
        set_node_allocation_type(node1, Tiny);

        BYTE* node2 = node1 + NODE_HEADER_SIZE + get_node_size(node1);
        set_node_size(node2, 26);
        set_previous_node_size(node2, get_node_size(node1));
        set_node_zone_start_offset(node2, get_node_zone_start_offset(node1) + NODE_HEADER_SIZE + get_node_size(node1));
        set_node_available(node2, TRUE);
        set_node_allocation_type(node2, Tiny);
        set_next_free_node(node1, node2);

        BYTE* node3 = node2 + NODE_HEADER_SIZE + get_node_size(node2);
        set_node_size(node3, 111);  // in 112 it will be separate
        set_previous_node_size(node3, get_node_size(node2));
        set_node_zone_start_offset(node3, get_node_zone_start_offset(node2) + NODE_HEADER_SIZE + get_node_size(node2));
        set_node_available(node3, TRUE);
        set_node_allocation_type(node3, Tiny);
        set_next_free_node(node2, node3);
        set_next_free_node(node3, nullptr);

        t_zone* zone = (t_zone*)test_zone;
        zone->last_allocated_node = (BYTE*)zone + zone->total_size - NODE_HEADER_SIZE + 16; // there is no space for any other node
        zone->total_size = TEST_ZONE_SIZE - ZONE_HEADER_SIZE;
        zone->first_free_node = node1;
        zone->last_free_node = node3;
        zone->next = nullptr;
        occupied_zone->next = zone;

        void* mem = take_memory_from_zone_list(occupied_zone, 48, 64, Tiny);
        ASSERT_TRUE(mem != nullptr);
        ASSERT_EQ(zone->first_free_node, node1);
        ASSERT_EQ(zone->last_free_node, node2);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, node1), node2);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, node2), nullptr);

        uint64_t node_zone_start_offset = ZONE_HEADER_SIZE + NODE_HEADER_SIZE * 2 + 38 + 26;
        BYTE* node = (BYTE*)zone + node_zone_start_offset;
        ASSERT_EQ(node + NODE_HEADER_SIZE, (BYTE*)mem);
        ASSERT_EQ(node, node3);
        ASSERT_EQ(get_node_size(node), 111);
        ASSERT_EQ(get_previous_node_size(node), 26);
        ASSERT_EQ(get_node_zone_start_offset(node), node_zone_start_offset);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, node), nullptr);
        ASSERT_EQ(get_node_available(node), FALSE);
        ASSERT_EQ(get_node_allocation_type(node), Tiny);
    }

    {
        /// check middle node
        bzero(test_zone, TEST_ZONE_SIZE);

        BYTE* node1 = test_zone + ZONE_HEADER_SIZE;
        set_node_size(node1, 38);
        set_previous_node_size(node1, 0);
        set_node_zone_start_offset(node1, ZONE_HEADER_SIZE);
        set_node_available(node1, TRUE);
        set_node_allocation_type(node1, Tiny);

        BYTE* node2 = node1 + NODE_HEADER_SIZE + get_node_size(node1);
        set_node_size(node2, 56);
        set_previous_node_size(node2, get_node_size(node1));
        set_node_zone_start_offset(node2, get_node_zone_start_offset(node1) + NODE_HEADER_SIZE + get_node_size(node1));
        set_node_available(node2, TRUE);
        set_node_allocation_type(node2, Tiny);
        set_next_free_node(node1, node2);

        BYTE* node3 = node2 + NODE_HEADER_SIZE + get_node_size(node2);
        set_node_size(node3, 64);  // in 112 it will be separate
        set_previous_node_size(node3, get_node_size(node2));
        set_node_zone_start_offset(node3, get_node_zone_start_offset(node2) + NODE_HEADER_SIZE + get_node_size(node2));
        set_node_available(node3, TRUE);
        set_node_allocation_type(node3, Tiny);
        set_next_free_node(node2, node3);
        set_next_free_node(node3, nullptr);

        t_zone* zone = (t_zone*)test_zone;
        zone->last_allocated_node = (BYTE*)zone + zone->total_size - NODE_HEADER_SIZE + 16; // there is no space for any other node
        zone->total_size = TEST_ZONE_SIZE - ZONE_HEADER_SIZE;
        zone->first_free_node = node1;
        zone->last_free_node = node3;
        zone->next = nullptr;
        occupied_zone->next = zone;

        void* mem = take_memory_from_zone_list(occupied_zone, 48, 64, Tiny);
        ASSERT_TRUE(mem != nullptr);
        ASSERT_EQ(zone->first_free_node, node1);
        ASSERT_EQ(zone->last_free_node, node3);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, node1), node3);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, node3), nullptr);

        uint64_t node_zone_start_offset = ZONE_HEADER_SIZE + NODE_HEADER_SIZE + 38;
        BYTE* node = (BYTE*)zone + node_zone_start_offset;
        ASSERT_EQ(node + NODE_HEADER_SIZE, (BYTE*)mem);
        ASSERT_EQ(node, node2);
        ASSERT_EQ(get_node_size(node), 56);
        ASSERT_EQ(get_previous_node_size(node), 38);
        ASSERT_EQ(get_node_zone_start_offset(node), node_zone_start_offset);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, node), nullptr);
        ASSERT_EQ(get_node_available(node), FALSE);
        ASSERT_EQ(get_node_allocation_type(node), Tiny);
    }
}

TEST(Take_Mem_From_Free_Nodes, Check_With_Separated) {
    t_zone* occupied_zone = (t_zone*)test_fully_occupied_zone;
    occupied_zone->first_free_node = nullptr;
    occupied_zone->last_free_node = nullptr;
    occupied_zone->total_size = TEST_ZONE_SIZE - ZONE_HEADER_SIZE;

    BYTE* last_allocated_node =
    (BYTE*)occupied_zone + (occupied_zone->total_size + ZONE_HEADER_SIZE) - NODE_HEADER_SIZE - 16;
    set_node_size(last_allocated_node, 16);
    set_previous_node_size(last_allocated_node, 16);
    set_node_zone_start_offset(last_allocated_node, occupied_zone->total_size - NODE_HEADER_SIZE - 16);
    set_next_free_node(last_allocated_node, 0);
    set_node_available(last_allocated_node, FALSE);
    set_node_allocation_type(last_allocated_node, Tiny);
    occupied_zone->last_allocated_node = last_allocated_node; // there is no space for any other node

    {
        /// check first node
        bzero(test_zone, TEST_ZONE_SIZE);

        BYTE* node1 = test_zone + ZONE_HEADER_SIZE;
        set_node_size(node1, 136);
        set_previous_node_size(node1, 0);
        set_node_zone_start_offset(node1, ZONE_HEADER_SIZE);
        set_node_available(node1, TRUE);
        set_node_allocation_type(node1, Tiny);

        BYTE* node2 = node1 + NODE_HEADER_SIZE + get_node_size(node1);
        set_node_size(node2, 162);
        set_previous_node_size(node2, get_node_size(node1));
        set_node_zone_start_offset(node2, get_node_zone_start_offset(node1) + NODE_HEADER_SIZE + get_node_size(node1));
        set_node_available(node2, TRUE);
        set_node_allocation_type(node2, Tiny);
        set_next_free_node(node1, node2);

        BYTE* node3 = node2 + NODE_HEADER_SIZE + get_node_size(node2);
        set_node_size(node3, 181);
        set_previous_node_size(node3, get_node_size(node2));
        set_node_zone_start_offset(node3, get_node_zone_start_offset(node2) + NODE_HEADER_SIZE + get_node_size(node2));
        set_node_available(node3, TRUE);
        set_node_allocation_type(node3, Tiny);
        set_next_free_node(node2, node3);
        set_next_free_node(node3, nullptr);

        t_zone* zone = (t_zone*)test_zone;
        zone->last_allocated_node = (BYTE*)zone + zone->total_size - NODE_HEADER_SIZE + 16; // there is no space for any other node
        zone->total_size = TEST_ZONE_SIZE - ZONE_HEADER_SIZE;
        zone->first_free_node = node1;
        zone->last_free_node = node3;
        zone->next = nullptr;
        occupied_zone->next = zone;

        void* mem = take_memory_from_zone_list(occupied_zone, 48, 64, Tiny);
        ASSERT_TRUE(mem != nullptr);

        BYTE* new_separated_node = (BYTE*)zone + ZONE_HEADER_SIZE + NODE_HEADER_SIZE + 48;
        ASSERT_EQ(get_node_size(new_separated_node), 136 - 48 - NODE_HEADER_SIZE);
        ASSERT_EQ(get_previous_node_size(new_separated_node), 48);
        ASSERT_EQ(get_node_zone_start_offset(new_separated_node), ZONE_HEADER_SIZE + NODE_HEADER_SIZE + 48);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, new_separated_node), node2);
        ASSERT_EQ(get_node_available(new_separated_node), TRUE);
        ASSERT_EQ(get_node_allocation_type(new_separated_node), Tiny);

        ASSERT_EQ(zone->first_free_node, new_separated_node);
        ASSERT_EQ(zone->last_free_node, node3);

        ASSERT_EQ(get_next_free_node((BYTE*)zone, node2), node3);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, node3), nullptr);

        BYTE* node = (BYTE*)zone + ZONE_HEADER_SIZE;
        ASSERT_EQ(node + NODE_HEADER_SIZE, (BYTE*)mem);
        ASSERT_EQ(node, node1);
        ASSERT_EQ(get_node_size(node), 48);
        ASSERT_EQ(get_previous_node_size(node), 0);
        ASSERT_EQ(get_node_zone_start_offset(node), ZONE_HEADER_SIZE);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, node), nullptr);
        ASSERT_EQ(get_node_available(node), FALSE);
        ASSERT_EQ(get_node_allocation_type(node), Tiny);
    }

    {
        /// check middle node
        BYTE* node1 = test_zone + ZONE_HEADER_SIZE;
        set_node_size(node1, 47);
        set_previous_node_size(node1, 0);
        set_node_zone_start_offset(node1, ZONE_HEADER_SIZE);
        set_node_available(node1, TRUE);
        set_node_allocation_type(node1, Tiny);

        BYTE* node2 = node1 + NODE_HEADER_SIZE + get_node_size(node1);
        set_node_size(node2, 162);
        set_previous_node_size(node2, get_node_size(node1));
        set_node_zone_start_offset(node2, get_node_zone_start_offset(node1) + NODE_HEADER_SIZE + get_node_size(node1));
        set_node_available(node2, TRUE);
        set_node_allocation_type(node2, Tiny);
        set_next_free_node(node1, node2);

        BYTE* node3 = node2 + NODE_HEADER_SIZE + get_node_size(node2);
        set_node_size(node3, 181);
        set_previous_node_size(node3, get_node_size(node2));
        set_node_zone_start_offset(node3, get_node_zone_start_offset(node2) + NODE_HEADER_SIZE + get_node_size(node2));
        set_node_available(node3, TRUE);
        set_node_allocation_type(node3, Tiny);
        set_next_free_node(node2, node3);
        set_next_free_node(node3, nullptr);

        t_zone* zone = (t_zone*)test_zone;
        zone->last_allocated_node = (BYTE*)zone + zone->total_size - NODE_HEADER_SIZE + 16; // there is no space for any other node
        zone->total_size = TEST_ZONE_SIZE - ZONE_HEADER_SIZE;
        zone->first_free_node = node1;
        zone->last_free_node = node3;
        zone->next = nullptr;
        occupied_zone->next = zone;

        void* mem = take_memory_from_zone_list(occupied_zone, 48, 64, Tiny);
        ASSERT_TRUE(mem != nullptr);

        uint64_t new_separated_node_zone_offset = ZONE_HEADER_SIZE + NODE_HEADER_SIZE * 2 + 47 + 48;
        BYTE* new_separated_node = (BYTE*)zone + new_separated_node_zone_offset;
        ASSERT_EQ(get_node_size(new_separated_node), 162 - 48 - NODE_HEADER_SIZE);
        ASSERT_EQ(get_previous_node_size(new_separated_node), 48);
        ASSERT_EQ(get_node_zone_start_offset(new_separated_node), new_separated_node_zone_offset);
        ASSERT_EQ(get_node_available(new_separated_node), TRUE);
        ASSERT_EQ(get_node_allocation_type(new_separated_node), Tiny);

        ASSERT_EQ(zone->first_free_node, node1);
        ASSERT_EQ(zone->last_free_node, node3);

        ASSERT_EQ(get_next_free_node((BYTE*)zone, node1), new_separated_node);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, new_separated_node), node3);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, node3), nullptr);

        uint64_t node_zone_offset = ZONE_HEADER_SIZE + NODE_HEADER_SIZE + 47;
        BYTE* node = (BYTE*)zone + node_zone_offset;
        ASSERT_EQ(node + NODE_HEADER_SIZE, (BYTE*)mem);
        ASSERT_EQ(node, node2);
        ASSERT_EQ(get_node_size(node), 48);
        ASSERT_EQ(get_previous_node_size(node), 47);
        ASSERT_EQ(get_node_zone_start_offset(node), node_zone_offset);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, node), nullptr);
        ASSERT_EQ(get_node_available(node), FALSE);
        ASSERT_EQ(get_node_allocation_type(node), Tiny);
    }

    {
        /// check last node
        BYTE* node1 = test_zone + ZONE_HEADER_SIZE;
        set_node_size(node1, 47);
        set_previous_node_size(node1, 0);
        set_node_zone_start_offset(node1, ZONE_HEADER_SIZE);
        set_node_available(node1, TRUE);
        set_node_allocation_type(node1, Tiny);

        BYTE* node2 = node1 + NODE_HEADER_SIZE + get_node_size(node1);
        set_node_size(node2, 22);
        set_previous_node_size(node2, get_node_size(node1));
        set_node_zone_start_offset(node2, get_node_zone_start_offset(node1) + NODE_HEADER_SIZE + get_node_size(node1));
        set_node_available(node2, TRUE);
        set_node_allocation_type(node2, Tiny);
        set_next_free_node(node1, node2);

        BYTE* node3 = node2 + NODE_HEADER_SIZE + get_node_size(node2);
        set_node_size(node3, 181);
        set_previous_node_size(node3, get_node_size(node2));
        set_node_zone_start_offset(node3, get_node_zone_start_offset(node2) + NODE_HEADER_SIZE + get_node_size(node2));
        set_node_available(node3, TRUE);
        set_node_allocation_type(node3, Tiny);
        set_next_free_node(node2, node3);
        set_next_free_node(node3, nullptr);

        t_zone* zone = (t_zone*)test_zone;
        zone->last_allocated_node = (BYTE*)zone + zone->total_size - NODE_HEADER_SIZE + 16; // there is no space for any other node
        zone->total_size = TEST_ZONE_SIZE - ZONE_HEADER_SIZE;
        zone->first_free_node = node1;
        zone->last_free_node = node3;
        zone->next = nullptr;
        occupied_zone->next = zone;

        void* mem = take_memory_from_zone_list(occupied_zone, 48, 64, Tiny);
        ASSERT_TRUE(mem != nullptr);

        uint64_t new_separated_node_zone_offset = ZONE_HEADER_SIZE + NODE_HEADER_SIZE * 3 + 47 + 22 + 48;
        BYTE* new_separated_node = (BYTE*)zone + new_separated_node_zone_offset;
        ASSERT_EQ(get_node_size(new_separated_node), 181 - 48 - NODE_HEADER_SIZE);
        ASSERT_EQ(get_previous_node_size(new_separated_node), 48);
        ASSERT_EQ(get_node_zone_start_offset(new_separated_node), new_separated_node_zone_offset);
        ASSERT_EQ(get_node_available(new_separated_node), TRUE);
        ASSERT_EQ(get_node_allocation_type(new_separated_node), Tiny);

        ASSERT_EQ(zone->first_free_node, node1);
        ASSERT_EQ(zone->last_free_node, new_separated_node);

        ASSERT_EQ(get_next_free_node((BYTE*)zone, node1), node2);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, node2), new_separated_node);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, new_separated_node), nullptr);

        uint64_t node_zone_offset = ZONE_HEADER_SIZE + NODE_HEADER_SIZE * 2 + 47 + 22;
        BYTE* node = (BYTE*)zone + node_zone_offset;
        ASSERT_EQ(node + NODE_HEADER_SIZE, (BYTE*)mem);
        ASSERT_EQ(node, node3);
        ASSERT_EQ(get_node_size(node), 48);
        ASSERT_EQ(get_previous_node_size(node), 22);
        ASSERT_EQ(get_node_zone_start_offset(node), node_zone_offset);
        ASSERT_EQ(get_next_free_node((BYTE*)zone, node), nullptr);
        ASSERT_EQ(get_node_available(node), FALSE);
        ASSERT_EQ(get_node_allocation_type(node), Tiny);
    }
}

TEST(Take_Memory_From_Zone, Main_Check) {
    t_zone* empty_zone = (t_zone*)test_fully_occupied_zone;
    empty_zone->first_free_node = nullptr;
    empty_zone->last_free_node = nullptr;
    empty_zone->total_size = 1337;
    empty_zone->available_size = 0;

    {
        /// check first occurrence
        bzero(test_zone, TEST_ZONE_SIZE);

        t_zone* zone = (t_zone*)test_zone;
        zone->available_size = TEST_ZONE_SIZE - sizeof(t_zone);
        zone->total_size = TEST_ZONE_SIZE - sizeof(t_zone);
        zone->first_free_node = nullptr;
        zone->last_free_node = nullptr;
        empty_zone->next = zone;
        zone->next = nullptr;

        void* mem = take_memory_from_zone_list(zone, 48, 64);
        ASSERT_TRUE(mem != nullptr);

        t_memory_node* node = (t_memory_node*)(CAST_TO_BYTE_APPLY_ZONE_SHIFT(test_zone));
        ASSERT_EQ(CAST_TO_BYTE_APPLY_NODE_SHIFT(node), (BYTE*)mem);
        ASSERT_EQ(node->available, false);
        ASSERT_EQ(node->usable_size, 48);
        ASSERT_EQ(node->next_free_node, nullptr);
        ASSERT_EQ(zone->available_size, TEST_ZONE_SIZE - sizeof(t_zone) - 48 - sizeof(t_memory_node));
        ASSERT_EQ(zone->total_size, TEST_ZONE_SIZE - sizeof(t_zone));
    }

    {
        /// check with not suitable free nodes
        bzero(test_zone, TEST_ZONE_SIZE);
        t_memory_node* node1 = (t_memory_node*)(CAST_TO_BYTE_APPLY_ZONE_SHIFT(test_zone));
        node1->usable_size = 47;
        node1->available = true;

        t_memory_node* node2 = (t_memory_node*)(CAST_TO_BYTE_APPLY_NODE_SHIFT(node1) + node1->usable_size);
        node2->usable_size = 22;
        node2->available = true;
        node1->next_free_node = node2;

        t_zone* zone = (t_zone*)test_zone;
        size_t start_zone_size =
                TEST_ZONE_SIZE - sizeof(t_zone) - SIZE_WITH_NODE_HEADER(47) - SIZE_WITH_NODE_HEADER(22);
        zone->available_size = start_zone_size;
        zone->total_size = TEST_ZONE_SIZE - sizeof(t_zone);
        zone->first_free_node = node1;
        zone->last_free_node = node2;
        empty_zone->next = zone;
        zone->next = nullptr;

        void* mem = take_memory_from_zone_list(zone, 48, 64);
        ASSERT_TRUE(mem != nullptr);

        ASSERT_EQ(zone->first_free_node, node1);
        ASSERT_EQ(zone->last_free_node, node2);

        t_memory_node* node = (t_memory_node*)(CAST_TO_BYTE_APPLY_ZONE_SHIFT(test_zone) + SIZE_WITH_NODE_HEADER(47) +
                                               SIZE_WITH_NODE_HEADER(22));
        ASSERT_EQ(CAST_TO_BYTE_APPLY_NODE_SHIFT(node), (BYTE*)mem);
        ASSERT_EQ(node->available, false);
        ASSERT_EQ(node->usable_size, 48);
        ASSERT_EQ(node->next_free_node, nullptr);
        ASSERT_EQ(zone->available_size, start_zone_size - SIZE_WITH_NODE_HEADER(48));
        ASSERT_EQ(zone->total_size, TEST_ZONE_SIZE - sizeof(t_zone));
    }
}

//TEST(Malloc, Check_Init_Correct) {
//    char* mem = (char*)malloc(5);
//
//    ASSERT_EQ(gInit, true);
//    ASSERT_EQ(gMemoryZones.first_large_allocation, nullptr);
//    ASSERT_EQ(gMemoryZones.last_large_allocation, nullptr);
//
//    ASSERT_EQ(gMemoryZones.first_tiny_zone->total_size, gPageSize * 4 - sizeof(t_zone));
//    ASSERT_EQ(gMemoryZones.first_tiny_zone->available_size, gPageSize * 4 - sizeof(t_zone) - SIZE_WITH_NODE_HEADER(16));
//    ASSERT_EQ(gMemoryZones.first_tiny_zone->first_free_node, nullptr);
//    ASSERT_EQ(gMemoryZones.first_tiny_zone->last_free_node, nullptr);
//    ASSERT_EQ(gMemoryZones.first_tiny_zone->next, nullptr);
//
//    ASSERT_EQ(gMemoryZones.last_tiny_zone->total_size, gPageSize * 4 - sizeof(t_zone));
//    ASSERT_EQ(gMemoryZones.last_tiny_zone->available_size, gPageSize * 4 - sizeof(t_zone) - SIZE_WITH_NODE_HEADER(16));
//    ASSERT_EQ(gMemoryZones.last_tiny_zone->first_free_node, nullptr);
//    ASSERT_EQ(gMemoryZones.last_tiny_zone->last_free_node, nullptr);
//    ASSERT_EQ(gMemoryZones.last_tiny_zone->next, nullptr);
//
//    ASSERT_EQ(gMemoryZones.first_small_zone->total_size, gPageSize * 16 - sizeof(t_zone));
//    ASSERT_EQ(gMemoryZones.first_small_zone->available_size, gPageSize * 16 - sizeof(t_zone));
//    ASSERT_EQ(gMemoryZones.first_small_zone->first_free_node, nullptr);
//    ASSERT_EQ(gMemoryZones.first_small_zone->last_free_node, nullptr);
//    ASSERT_EQ(gMemoryZones.first_small_zone->next, nullptr);
//
//    ASSERT_EQ(gMemoryZones.last_small_zone->total_size, gPageSize * 16 - sizeof(t_zone));
//    ASSERT_EQ(gMemoryZones.last_small_zone->available_size, gPageSize * 16 - sizeof(t_zone));
//    ASSERT_EQ(gMemoryZones.last_small_zone->first_free_node, nullptr);
//    ASSERT_EQ(gMemoryZones.last_small_zone->last_free_node, nullptr);
//    ASSERT_EQ(gMemoryZones.last_small_zone->next, nullptr);
//}
//
//TEST(Malloc, Correct_Zone_Select) {
//    free_all();
//    {
//        /// tiny zone choosing and adding correct
//        for (size_t i = 0; i < (gTinyZoneSize - sizeof(t_zone)) / (SIZE_WITH_NODE_HEADER(gTinyAllocationMaxSize)); ++i) {
//            void* mem = malloc(gTinyAllocationMaxSize);
//        }
//        ASSERT_EQ(gMemoryZones.first_tiny_zone->available_size,
//                  (gTinyZoneSize - sizeof(t_zone)) % SIZE_WITH_NODE_HEADER(gTinyAllocationMaxSize));
//        ASSERT_EQ(gMemoryZones.first_tiny_zone->next, nullptr);
//        ASSERT_EQ(gMemoryZones.first_small_zone->available_size, gSmallZoneSize - sizeof(t_zone));
//        ASSERT_EQ(gMemoryZones.first_large_allocation, nullptr);
//
//        void* mem = malloc(gTinyAllocationMaxSize);
//        t_zone* zone = gMemoryZones.last_tiny_zone;
//
//        ASSERT_FALSE(zone == gMemoryZones.first_tiny_zone);
//        ASSERT_EQ(gMemoryZones.first_tiny_zone->next, zone);
//        ASSERT_EQ(zone->available_size,
//                  gTinyZoneSize - sizeof(t_zone) - SIZE_WITH_NODE_HEADER(gTinyAllocationMaxSize));
//        ASSERT_EQ(gMemoryZones.first_small_zone->available_size, gSmallZoneSize - sizeof(t_zone));
//        ASSERT_EQ(gMemoryZones.first_large_allocation, nullptr);
//    }
//
//    {
//        /// small zone choosing and adding correct
//        for (size_t i = 0; i < (gSmallZoneSize - sizeof(t_zone)) / (SIZE_WITH_NODE_HEADER(gSmallAllocationMaxSize)); ++i) {
//            void* mem = malloc(gSmallAllocationMaxSize);
//        }
//        ASSERT_EQ(gMemoryZones.first_small_zone->available_size,
//                  (gSmallZoneSize - sizeof(t_zone)) % SIZE_WITH_NODE_HEADER(gSmallAllocationMaxSize));
//        ASSERT_EQ(gMemoryZones.first_small_zone->next, nullptr);
//        ASSERT_EQ(gMemoryZones.first_large_allocation, nullptr);
//
//        void* mem = malloc(gSmallAllocationMaxSize);
//        t_zone* zone = gMemoryZones.last_small_zone;
//
//        ASSERT_FALSE(zone == gMemoryZones.first_small_zone);
//        ASSERT_EQ(gMemoryZones.first_small_zone->next, zone);
//        ASSERT_EQ(zone->available_size,
//                  gSmallZoneSize - sizeof(t_zone) - SIZE_WITH_NODE_HEADER(gSmallAllocationMaxSize));
//        ASSERT_EQ(gMemoryZones.first_large_allocation, nullptr);
//    }
//
//    {
//        /// large allocation choosing and adding correct
//        size_t mem_size = gSmallAllocationMaxSize + 16;
//        void* mem = malloc(mem_size);
//        ASSERT_EQ(gMemoryZones.first_large_allocation->available_size, mem_size + gPageSize - mem_size % gPageSize - sizeof(t_zone));
//        ASSERT_EQ(gMemoryZones.first_large_allocation->last_free_node->usable_size, mem_size + gPageSize - mem_size % gPageSize - sizeof(t_zone) - sizeof(t_memory_node));
//
//        mem_size = gSmallAllocationMaxSize + gPageSize;
//        mem = malloc(mem_size);
//
//        t_zone* zone = gMemoryZones.last_large_allocation;
//        ASSERT_FALSE(zone == gMemoryZones.first_large_allocation);
//        ASSERT_EQ(gMemoryZones.first_large_allocation->next, zone);
//        ASSERT_EQ(zone->available_size, mem_size + gPageSize - mem_size % gPageSize - sizeof(t_zone));
//        ASSERT_EQ(zone->first_free_node->usable_size, mem_size + gPageSize - mem_size % gPageSize - sizeof(t_zone) - sizeof(t_memory_node));
//    }
//}
//
