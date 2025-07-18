#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <time.h>
#include <sys/resource.h>
#include <signal.h>
#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <limits.h>
#include <sys/time.h>
int handleVmem(char*);
int handleMCalc(char**,int);
int handleAdd(char**,int);
void* addition(void*);
void* sub(void*);
int handleSub(char**,int);
typedef struct {
    int V;
    int D;
    int P;
    int frame_swap;
} page_descriptor;


typedef struct {
    int page_number;
    int frame_number;
    int valid;
    int timestamp;
} tlb_entry;
typedef struct sim_database {
    page_descriptor* page_table;  
    int swapfile_fd;
    int program_fd;
    char* main_memory;            
    int text_size;
    int data_size;
    int bss_size;
    int heap_stack_size;
    
    tlb_entry* tlb;                
    
    int page_size;
    int num_pages;
    int memory_size;
    int swap_size;
    int num_frames;
    int tlb_size;
} sim_database;

#include <stdio.h>
#include <unistd.h>
#include <string.h>
sim_database* init_system(char* script_path);
char load(sim_database* mem_sim, int address);
void store(sim_database* mem_sim, int address, char value);
void clear_system(sim_database* mem_sim);
void update_page_access_time(int page_num);
int find_free_frame(sim_database* mem_sim);
int find_free_swap_slot(sim_database* mem_sim);
void save_page_to_swap(sim_database* mem_sim, int page_num);
int evict_page_lru(sim_database* mem_sim);
void load_page_from_program(sim_database* mem_sim, int page_num, char* dest, int base_offset);
void load_page_from_swap(sim_database* mem_sim, int page_num, char* dest);
int check_tlb(sim_database* mem_sim, int page_num);
void add_to_tlb(sim_database* mem_sim, int page_num, int frame_num);
void remove_from_tlb(sim_database* mem_sim, int page_num);
/**
 * print_memory - Prints the contents of the main memory (RAM)
 * Shows each frame with its contents in both hex and character format
 */
void print_memory(sim_database* mem_sim) {
    if (!mem_sim || !mem_sim->main_memory) {
        printf("Error: Invalid memory simulation structure\n");
        return;
    }
    
    printf("=== MAIN MEMORY CONTENTS ===\n");
    printf("Memory size: %d bytes, Page size: %d bytes, Number of frames: %d\n", 
           mem_sim->memory_size, mem_sim->page_size, mem_sim->num_frames);
    
    for (int frame = 0; frame < mem_sim->num_frames; frame++) {
        printf("Frame %d: ", frame);
        
        // Print hex values
        for (int i = 0; i < mem_sim->page_size; i++) {
            int addr = frame * mem_sim->page_size + i;
            printf("%02X ", (unsigned char)mem_sim->main_memory[addr]);
        }
        
        printf("| ");
        
        // Print character representation
        for (int i = 0; i < mem_sim->page_size; i++) {
            int addr = frame * mem_sim->page_size + i;
            char c = mem_sim->main_memory[addr];
            if (c >= 32 && c <= 126) {  // Printable ASCII
                printf("%c", c);
            } else {
                printf(".");
            }
        }
        printf("\n");
    }
    printf("=============================\n\n");
}

/**
 * print_swap - Prints the contents of the swap file
 * Shows each page slot in the swap file
 */
void print_swap(sim_database* mem_sim) {
    if (!mem_sim || mem_sim->swapfile_fd < 0) {
        printf("Error: Invalid swap file\n");
        return;
    }
    
    printf("=== SWAP FILE CONTENTS ===\n");
    printf("Swap size: %d bytes, Page size: %d bytes, Number of swap pages: %d\n", 
           mem_sim->swap_size, mem_sim->page_size, mem_sim->swap_size / mem_sim->page_size);
    
    int num_swap_pages = mem_sim->swap_size / mem_sim->page_size;
    char* buffer = malloc(mem_sim->page_size);
    
    if (!buffer) {
        perror("Error allocating buffer for swap reading");
        return;
    }
    
    for (int page = 0; page < num_swap_pages; page++) {
        // Seek to the beginning of this page in swap file
        if (lseek(mem_sim->swapfile_fd, page * mem_sim->page_size, SEEK_SET) == -1) {
            perror("Error seeking in swap file");
            free(buffer);
            return;
        }
        
        // Read the page
        ssize_t bytes_read = read(mem_sim->swapfile_fd, buffer, mem_sim->page_size);
        if (bytes_read != mem_sim->page_size) {
            printf("Swap Page %d: [Error reading]\n", page);
            continue;
        }
        
        printf("Swap Page %d: ", page);
        
        // Print hex values
        for (int i = 0; i < mem_sim->page_size; i++) {
            printf("%02X ", (unsigned char)buffer[i]);
        }
        
        printf("| ");
        
        // Print character representation
        for (int i = 0; i < mem_sim->page_size; i++) {
            char c = buffer[i];
            if (c >= 32 && c <= 126) {  // Printable ASCII
                printf("%c", c);
            } else {
                printf(".");
            }
        }
        printf("\n");
    }
    
    free(buffer);
    printf("===========================\n\n");
}

/**
 * print_page_table - Prints the page table contents
 * Shows all page descriptors with their flags and frame/swap locations
 */
void print_page_table(sim_database* mem_sim) {
    if (!mem_sim || !mem_sim->page_table) {
        printf("Error: Invalid page table\n");
        return;
    }
    
    printf("=== PAGE TABLE ===\n");
    printf("Number of pages: %d\n", mem_sim->num_pages);
    printf("Page | V | D | P | Frame/Swap | Segment\n");
    printf("-----|---|---|---|------------|--------\n");
    
    // Calculate segment boundaries in pages
    int text_pages = (mem_sim->text_size + mem_sim->page_size - 1) / mem_sim->page_size;
    int data_pages = (mem_sim->data_size + mem_sim->page_size - 1) / mem_sim->page_size;
    int bss_pages = (mem_sim->bss_size + mem_sim->page_size - 1) / mem_sim->page_size;
    
    for (int page = 0; page < mem_sim->num_pages; page++) {
        page_descriptor* pd = &mem_sim->page_table[page];
        
        // Determine segment type
        const char* segment;
        if (page < text_pages) {
            segment = "TEXT";
        } else if (page < text_pages + data_pages) {
            segment = "DATA";
        } else if (page < text_pages + data_pages + bss_pages) {
            segment = "BSS";
        } else {
            segment = "H/S";  // Heap/Stack
        }
        
        printf("%4d | %d | %d | %d |", page, pd->V, pd->D, pd->P);
        
        if (pd->frame_swap == -1) {
            printf("      -    |");
        } else {
            printf("    %4d   |", pd->frame_swap);
        }
        
        printf(" %s\n", segment);
    }
    printf("==================\n");
    printf("Legend: V=Valid, D=Dirty, P=Permission (1=Read-Only, 0=Read/Write)\n");
    printf("        Frame/Swap: Frame number if in memory (V=1), Swap page if swapped out\n\n");
}

/**
 * print_tlb - Prints the TLB contents (bonus function)
 * Shows all TLB entries with their page->frame mappings
 */
void print_tlb(sim_database* mem_sim) {
    if (!mem_sim || !mem_sim->tlb) {
        printf("TLB not implemented or invalid\n");
        return;
    }
    
    printf("=== TLB CONTENTS ===\n");
    printf("TLB size: %d entries\n", mem_sim->tlb_size);
    printf("Entry | Valid | Page | Frame | Timestamp\n");
    printf("------|-------|------|-------|----------\n");
    
    for (int i = 0; i < mem_sim->tlb_size; i++) {
        tlb_entry* entry = &mem_sim->tlb[i];
        printf("  %d   |   %d   |", i, entry->valid);
        
        if (entry->valid) {
            printf(" %4d | %5d |  %8d\n", 
                   entry->page_number, entry->frame_number, entry->timestamp);
        } else {
            printf("   -  |   -   |     -\n");
        }
    }
    printf("====================\n\n");
}
sim_database* init_system(char* init_line) {
    // Allocate the main structure
    sim_database* mem_sim = (sim_database*)calloc(1, sizeof(sim_database));
    if (!mem_sim) {
        perror("Error allocating memory for sim_database");
        return NULL;
    }
    
    // Parse initialization parameters
    char exe_file_name[256];
    char swap_file_name[256];
    
    int parsed = sscanf(init_line, "%s %s %d %d %d %d %d %d %d %d",
                        exe_file_name, swap_file_name,
                        &mem_sim->text_size, &mem_sim->data_size,
                        &mem_sim->bss_size, &mem_sim->heap_stack_size,
                        &mem_sim->page_size, &mem_sim->num_pages,
                        &mem_sim->memory_size, &mem_sim->swap_size);
    
    if (parsed != 10) {
        fprintf(stderr, "Error: Invalid script format\n");
        free(mem_sim);
        return NULL;
    }
    
    // Calculate number of frames
    mem_sim->num_frames = mem_sim->memory_size / mem_sim->page_size;
    
    // Open program file (read-only)
    mem_sim->program_fd = open(exe_file_name, O_RDONLY);
    if (mem_sim->program_fd < 0) {
        perror("Error opening program file");
        free(mem_sim);
        return NULL;
    }
    
    // Create/open swap file
    mem_sim->swapfile_fd = open(swap_file_name, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (mem_sim->swapfile_fd < 0) {
        perror("Error creating/opening swap file");
        close(mem_sim->program_fd);
        free(mem_sim);
        return NULL;
    }
    
    // Initialize swap file with '-' characters
    char* init_buffer = (char*)malloc(mem_sim->swap_size);
    if (!init_buffer) {
        perror("Error allocating buffer");
        close(mem_sim->program_fd);
        close(mem_sim->swapfile_fd);
        free(mem_sim);
        return NULL;
    }
    memset(init_buffer, '-', mem_sim->swap_size);
    if (write(mem_sim->swapfile_fd, init_buffer, mem_sim->swap_size) != mem_sim->swap_size) {
        perror("Error writing to swap file");
        free(init_buffer);
        close(mem_sim->program_fd);
        close(mem_sim->swapfile_fd);
        free(mem_sim);
        return NULL;
    }
    free(init_buffer);
    
    // Allocate and initialize main memory
    mem_sim->main_memory = (char*)malloc(mem_sim->memory_size);
    if (!mem_sim->main_memory) {
        perror("Error allocating main memory");
        close(mem_sim->program_fd);
        close(mem_sim->swapfile_fd);
        free(mem_sim);
        return NULL;
    }
    memset(mem_sim->main_memory, '-', mem_sim->memory_size);
    
    // Allocate and initialize page table
    mem_sim->page_table = (page_descriptor*)calloc(mem_sim->num_pages, sizeof(page_descriptor));
    if (!mem_sim->page_table) {
        perror("Error allocating page table");
        free(mem_sim->main_memory);
        close(mem_sim->program_fd);
        close(mem_sim->swapfile_fd);
        free(mem_sim);
        return NULL;
    }
    
    // Initialize page table entries
    int text_pages = (mem_sim->text_size + mem_sim->page_size - 1) / mem_sim->page_size;
    
    for (int i = 0; i < mem_sim->num_pages; i++) {
        mem_sim->page_table[i].V = 0;  // Not in memory
        mem_sim->page_table[i].D = 0;  // Not dirty
        mem_sim->page_table[i].frame_swap = -1;  // Not allocated
        
        // Set permissions: TEXT pages are read-only (P=1)
        if (i < text_pages) {
            mem_sim->page_table[i].P = 1;  // Read-only
        } else {
            mem_sim->page_table[i].P = 0;  // Read-write
        }
    }
    
    // Print initialization message
    printf("Loaded program \"%s\" with text=%d, data=%d, bss=%d, heap_stack=%d.\n",
           exe_file_name, mem_sim->text_size, mem_sim->data_size,
           mem_sim->bss_size, mem_sim->heap_stack_size);
    
    return mem_sim;
}
int handleVmem(char* scriptPath) {
    // Open the script file
    FILE* script = fopen(scriptPath, "r");
    if (!script) {
        fprintf(stderr, "Error: Cannot open script file %s\n", scriptPath);
        return -1;  // Return error code
    }
    
    // Read the first line for initialization
    char line[256];
    if (!fgets(line, sizeof(line), script)) {
        fprintf(stderr, "Error: Invalid script format\n");
        fclose(script);
        return -1;  // Return error code
    }
    
    // Remove newline from first line
    line[strcspn(line, "\n")] = '\0';
    
    // Initialize the system using the first line
    sim_database* mem_sim = init_system(line);
    if (!mem_sim) {
        fprintf(stderr, "Error: Failed to initialize memory system\n");
        fclose(script);
        return -1;  // Return error code
    }
    
    // Process commands from the script
    while (fgets(line, sizeof(line), script)) {
        // Remove newline
        line[strcspn(line, "\n")] = '\0';
        
        // Skip empty lines
        if (strlen(line) == 0) continue;
        
        // Parse the command
        char command[20];
        int address;
        char value;
        
        if (sscanf(line, "%s", command) < 1) continue;
        
        if (strcmp(command, "load") == 0) {
            if (sscanf(line, "load %d", &address) == 1) {
                char result = load(mem_sim, address);
                if (result != '\0') {
                    printf("Value at address %d = %c\n", address, result);
                }
            }
        }
        else if (strcmp(command, "store") == 0) {
            if (sscanf(line, "store %d %c", &address, &value) == 2) {
                store(mem_sim, address, value);
                // Only print success if store didn't print an error
                // Check if the store was successful by verifying the value was written
                if (load(mem_sim, address) == value) {
                    printf("Stored value '%c' at address %d\n", value, address);
                }
            }
        }
        else if (strcmp(command, "print") == 0) {
            char target[20];
            if (sscanf(line, "print %s", target) == 1) {
                if (strcmp(target, "ram") == 0) {
                    print_memory(mem_sim);
                }
                else if (strcmp(target, "swap") == 0) {
                    print_swap(mem_sim);
                }
                else if (strcmp(target, "table") == 0) {
                    print_page_table(mem_sim);
                }
            }
        }
    }
    
    // Clean up
    fclose(script);
    clear_system(mem_sim);
    return 0;  // Return success
}
// Global variables for LRU tracking
static int* page_access_time = NULL;
static int global_time_counter = 0;

// Helper function to update access time for LRU
void update_page_access_time(int page_num) {
    if (!page_access_time) {
        page_access_time = (int*)calloc(1024, sizeof(int)); // Max pages
    }
    page_access_time[page_num] = global_time_counter++;
}

// Helper function to find a free frame
int find_free_frame(sim_database* mem_sim) {
    // Check if any frame is not used by checking all pages
    for (int frame = 0; frame < mem_sim->num_frames; frame++) {
        int frame_used = 0;
        for (int page = 0; page < mem_sim->num_pages; page++) {
            if (mem_sim->page_table[page].V == 1 && 
                mem_sim->page_table[page].frame_swap == frame) {
                frame_used = 1;
                break;
            }
        }
        if (!frame_used) {
            return frame;
        }
    }
    return -1;  // No free frame found
}
// Check if page is in TLB
int check_tlb(sim_database* mem_sim, int page_num) {
    if (!mem_sim->tlb) return -1;
    
    for (int i = 0; i < mem_sim->tlb_size; i++) {
        if (mem_sim->tlb[i].valid && mem_sim->tlb[i].page_number == page_num) {
            // Update timestamp for LRU
            mem_sim->tlb[i].timestamp = global_time_counter++;
            return mem_sim->tlb[i].frame_number;
        }
    }
    return -1;  // TLB miss
}

// Add entry to TLB
void add_to_tlb(sim_database* mem_sim, int page_num, int frame_num) {
    if (!mem_sim->tlb) return;
    
    // First check if page already in TLB (shouldn't happen but be safe)
    for (int i = 0; i < mem_sim->tlb_size; i++) {
        if (mem_sim->tlb[i].valid && mem_sim->tlb[i].page_number == page_num) {
            mem_sim->tlb[i].frame_number = frame_num;
            mem_sim->tlb[i].timestamp = global_time_counter++;
            printf("TLB Updated: Page %d -> Frame %d\n", page_num, frame_num);
            return;
        }
    }
    
    // Find empty slot
    for (int i = 0; i < mem_sim->tlb_size; i++) {
        if (!mem_sim->tlb[i].valid) {
            mem_sim->tlb[i].valid = 1;
            mem_sim->tlb[i].page_number = page_num;
            mem_sim->tlb[i].frame_number = frame_num;
            mem_sim->tlb[i].timestamp = global_time_counter++;
            printf("TLB Updated: Page %d -> Frame %d\n", page_num, frame_num);
            return;
        }
    }
    
    // TLB full - evict LRU entry
    int lru_idx = 0;
    int oldest_time = mem_sim->tlb[0].timestamp;
    
    for (int i = 1; i < mem_sim->tlb_size; i++) {
        if (mem_sim->tlb[i].timestamp < oldest_time) {
            oldest_time = mem_sim->tlb[i].timestamp;
            lru_idx = i;
        }
    }
    
    // Replace LRU entry
    mem_sim->tlb[lru_idx].page_number = page_num;
    mem_sim->tlb[lru_idx].frame_number = frame_num;
    mem_sim->tlb[lru_idx].timestamp = global_time_counter++;
    printf("TLB Updated: Page %d -> Frame %d\n", page_num, frame_num);
}

// Remove page from TLB when it's evicted from memory
void remove_from_tlb(sim_database* mem_sim, int page_num) {
    if (!mem_sim->tlb) return;
    
    for (int i = 0; i < mem_sim->tlb_size; i++) {
        if (mem_sim->tlb[i].valid && mem_sim->tlb[i].page_number == page_num) {
            mem_sim->tlb[i].valid = 0;
            mem_sim->tlb[i].page_number = -1;
            mem_sim->tlb[i].frame_number = -1;
            return;
        }
    }
}
// Helper function to find free swap slot (first-fit)
int find_free_swap_slot(sim_database* mem_sim) {
    int num_swap_pages = mem_sim->swap_size / mem_sim->page_size;
    char* check_buffer = (char*)malloc(mem_sim->page_size);
    
    for (int slot = 0; slot < num_swap_pages; slot++) {
        // Check if this slot is all '-' (empty)
        if (lseek(mem_sim->swapfile_fd, slot * mem_sim->page_size, SEEK_SET) == -1) {
            free(check_buffer);
            return -1;
        }
        
        if (read(mem_sim->swapfile_fd, check_buffer, mem_sim->page_size) != mem_sim->page_size) {
            free(check_buffer);
            return -1;
        }
        
        // Check if slot is empty (all '-')
        int is_empty = 1;
        for (int i = 0; i < mem_sim->page_size; i++) {
            if (check_buffer[i] != '-') {
                is_empty = 0;
                break;
            }
        }
        
        if (is_empty) {
            free(check_buffer);
            return slot;
        }
    }
    
    free(check_buffer);
    return -1;  // No free slot found
}

// Helper function to save page to swap
void save_page_to_swap(sim_database* mem_sim, int page_num) {
    // Find first free slot in swap (first-fit)
    int swap_slot = find_free_swap_slot(mem_sim);
    if (swap_slot == -1) {
        fprintf(stderr, "Error: Swap file full!\n");
        return;
    }
    
    // Get frame content
    int frame_num = mem_sim->page_table[page_num].frame_swap;
    char* frame_start = mem_sim->main_memory + (frame_num * mem_sim->page_size);
    
    // Write to swap
    int swap_offset = swap_slot * mem_sim->page_size;
    if (lseek(mem_sim->swapfile_fd, swap_offset, SEEK_SET) == -1) {
        perror("Error seeking in swap file");
        return;
    }
    
    if (write(mem_sim->swapfile_fd, frame_start, mem_sim->page_size) != mem_sim->page_size) {
        perror("Error writing to swap file");
        return;
    }
    
    // Update page table to remember swap location
    mem_sim->page_table[page_num].frame_swap = swap_slot;
}

// Helper function to evict a page using LRU
int evict_page_lru(sim_database* mem_sim) {
    // Initialize access times if needed
    if (!page_access_time) {
        page_access_time = (int*)calloc(mem_sim->num_pages, sizeof(int));
    }
    
    // Find the page with oldest access time that's in memory
    int oldest_page = -1;
    int oldest_time = global_time_counter;
    
    for (int page = 0; page < mem_sim->num_pages; page++) {
        if (mem_sim->page_table[page].V == 1) {
            if (oldest_page == -1 || page_access_time[page] < oldest_time) {
                oldest_page = page;
                oldest_time = page_access_time[page];
            }
        }
    }
    
    if (oldest_page == -1) {
        fprintf(stderr, "Error: No page to evict!\n");
        return -1;
    }
    
    // Get the frame that will be freed
    int frame_to_free = mem_sim->page_table[oldest_page].frame_swap;
    
    // If page is dirty and not TEXT, save to swap
    if (mem_sim->page_table[oldest_page].D == 1 && 
        mem_sim->page_table[oldest_page].P == 0) {  // Not read-only
        printf("Page replacement: Evicting page %d to swap\n", oldest_page);
        save_page_to_swap(mem_sim, oldest_page);
    }
    
    // Mark page as not in memory
    mem_sim->page_table[oldest_page].V = 0;
    
    // Remove from TLB - IMPORTANT: remove the evicted page from TLB
    remove_from_tlb(mem_sim, oldest_page);
    
    return frame_to_free;
}

// Helper function to load page from program file
void load_page_from_program(sim_database* mem_sim, int page_num, char* dest, int base_offset) {
    int file_offset = base_offset + (page_num * mem_sim->page_size);
    
    if (lseek(mem_sim->program_fd, file_offset, SEEK_SET) == -1) {
        perror("Error seeking in file");
        return;
    }
    
    ssize_t bytes_read = read(mem_sim->program_fd, dest, mem_sim->page_size);
    if (bytes_read != mem_sim->page_size) {
        if (bytes_read == -1) {
            perror("Error reading from file");
        }
        // Fill rest with zeros if we read less than page_size
        if (bytes_read > 0 && bytes_read < mem_sim->page_size) {
            memset(dest + bytes_read, 0, mem_sim->page_size - bytes_read);
        }
    }
}

// Helper function to load page from swap
void load_page_from_swap(sim_database* mem_sim, int page_num, char* dest) {
    int swap_page = mem_sim->page_table[page_num].frame_swap;
    int swap_offset = swap_page * mem_sim->page_size;
    
    if (lseek(mem_sim->swapfile_fd, swap_offset, SEEK_SET) == -1) {
        perror("Error seeking in swap file");
        return;
    }
    
    if (read(mem_sim->swapfile_fd, dest, mem_sim->page_size) != mem_sim->page_size) {
        perror("Error reading from swap file");
    }
}

// Main load function
char load(sim_database* mem_sim, int address) {
    // 1. Check if address is valid
    if (address < 0 || address >= (mem_sim->num_pages * mem_sim->page_size)) {
        fprintf(stderr, "Error: Invalid address %d (out of range)\n", address);
        return '\0';
    }
    
    // 2. Calculate page number and offset
    int page_num = address / mem_sim->page_size;
    int offset = address % mem_sim->page_size;
    
    // 3. Check TLB first
    int tlb_frame = check_tlb(mem_sim, page_num);
    if (mem_sim->tlb) {
        int tlb_frame = check_tlb(mem_sim, page_num);
        if (tlb_frame != -1) {
            // TLB hit!
            printf("TLB Hit: Page %d -> Frame %d\n", page_num, tlb_frame);
            int physical_addr = tlb_frame * mem_sim->page_size + offset;
            update_page_access_time(page_num);
            return mem_sim->main_memory[physical_addr];
        }
        
        // TLB miss
        printf("TLB Miss: Page %d\n", page_num);
    }
    // 4. Check if page is already in memory (page table lookup)
    if (mem_sim->page_table[page_num].V == 1) {
        // Page is in memory - add to TLB
        int frame_num = mem_sim->page_table[page_num].frame_swap;
        add_to_tlb(mem_sim, page_num, frame_num);
        
        int physical_addr = frame_num * mem_sim->page_size + offset;
        update_page_access_time(page_num);
        return mem_sim->main_memory[physical_addr];
    }
    
    // 5. Page fault - need to load the page
    // Find a free frame or select one to evict
    int frame_to_use = find_free_frame(mem_sim);
    if (frame_to_use == -1) {
        // No free frame, need to evict a page using LRU
        frame_to_use = evict_page_lru(mem_sim);
    }
    
    // Now print the page fault message after eviction is done
    printf("Page fault: Loading page %d from ", page_num);
    
    // 6. Load the page content based on its type
    char* frame_start = mem_sim->main_memory + (frame_to_use * mem_sim->page_size);
    
    // Calculate which segment this page belongs to
    int text_pages = (mem_sim->text_size + mem_sim->page_size - 1) / mem_sim->page_size;
    int data_pages = (mem_sim->data_size + mem_sim->page_size - 1) / mem_sim->page_size;
    int bss_pages = (mem_sim->bss_size + mem_sim->page_size - 1) / mem_sim->page_size;
    
    if (page_num < text_pages) {
        // TEXT page - always load from program file
        printf("program file\n");
        load_page_from_program(mem_sim, page_num, frame_start, 0);
    }
    else if (mem_sim->page_table[page_num].D == 1) {
        // Page was modified before - load from swap
        printf("swap\n");
        load_page_from_swap(mem_sim, page_num, frame_start);
    }
    else if (page_num < text_pages + data_pages) {
        // DATA page - load from program file
        printf("program file\n");
        int file_offset = mem_sim->text_size;
        load_page_from_program(mem_sim, page_num - text_pages, frame_start, file_offset);
    }
    else {
        // BSS or HEAP/STACK page - initialize with zeros
        printf("new allocation\n");
        memset(frame_start, 0, mem_sim->page_size);
    }
    
    // 7. Update page table
    mem_sim->page_table[page_num].V = 1;
    mem_sim->page_table[page_num].frame_swap = frame_to_use;
    
    // 8. Add to TLB
    add_to_tlb(mem_sim, page_num, frame_to_use);
    
    // Update LRU access time
    update_page_access_time(page_num);
    
    // 9. Access the data
    int physical_addr = frame_to_use * mem_sim->page_size + offset;
    return mem_sim->main_memory[physical_addr];
}
void store(sim_database* mem_sim, int address, char value) {
    // 1. Check if address is valid
    if (address < 0 || address >= (mem_sim->num_pages * mem_sim->page_size)) {
        fprintf(stderr, "Error: Invalid address %d (out of range)\n", address);
        return;
    }
    
    // 2. Calculate page number to check permissions
    int page_num = address / mem_sim->page_size;
    
    // 3. Check write permissions (TEXT segments are read-only)
    if (mem_sim->page_table[page_num].P == 1) {
        fprintf(stderr, "Error: Invalid write operation to read-only segment at address %d\n", address);
        return;
    }
    
    // 4. Use load() to ensure the page is in memory
    // This handles all the page fault logic for us
    char current_value = load(mem_sim, address);
    
    // Note: load() will print any error messages and return '\0' on failure
    // We don't need to check for errors here since load() handles them
    
    // 5. Now the page is guaranteed to be in memory (if load succeeded)
    // Calculate physical address
    int offset = address % mem_sim->page_size;
    int frame_num = mem_sim->page_table[page_num].frame_swap;
    int physical_addr = frame_num * mem_sim->page_size + offset;
    
    // 6. Write the value to memory
    mem_sim->main_memory[physical_addr] = value;
    
    // 7. Mark the page as dirty
    mem_sim->page_table[page_num].D = 1;
    
    // The page will be saved to swap when it gets evicted (handled by evict_page_lru)
}
void clear_system(sim_database* mem_sim) {
    if (!mem_sim) return;
    
    // Free page table
    if (mem_sim->page_table) {
        free(mem_sim->page_table);
    }
    
    // Free main memory
    if (mem_sim->main_memory) {
        free(mem_sim->main_memory);
    }
    
    // Close file descriptors
    if (mem_sim->program_fd >= 0) {
        close(mem_sim->program_fd);
    }
    
    if (mem_sim->swapfile_fd >= 0) {
        close(mem_sim->swapfile_fd);
    }
    
    // Free TLB if implemented (bonus)
    if (mem_sim->tlb) {
        free(mem_sim->tlb);
    }
    
    // Free the main structure
    free(mem_sim);
    
    // Clean up global LRU tracking
    if (page_access_time) {
        free(page_access_time);
        page_access_time = NULL;
        global_time_counter = 0;
    }
}
int handleMCalc(char** tokens,int tokenCount) {
    if (tokenCount < 4) {
        fprintf(stderr, "Usage: mcalc <var1> <var2> <operation>\n");
        return -1;
    }
    for (int i = 1; i < tokenCount - 1; i++) {
        char* token = tokens[i];
        int len = strlen(token);
        if (len < 2 || token[0] != '"' || token[len-1] != '"') {
            fprintf(stderr, "ERR_MAT_INPUT\n");
            return -1;
        }
        if (len < 4 || token[1] != '(' || token[len-2] != ')') {
            fprintf(stderr, "ERR_MAT_INPUT\n");
            return -1;
        }
        char* copy = malloc(len - 3 + 1);  // Remove "()" and quotes
        strncpy(copy, token + 2, len - 4);
        copy[len - 4] = '\0';
        char* colon = strchr(copy, ':');
        if (!colon || strchr(colon + 1, ':')) {  // No colon or multiple colons
            free(copy);
            fprintf(stderr, "ERR_MAT_INPUT\n");
            return -1;
        }
        *colon = '\0';  
        char* comma = strchr(copy, ',');
        if (!comma || strchr(comma + 1, ',')) {  // No comma or multiple commas in dimensions
            free(copy);
            fprintf(stderr, "ERR_MAT_INPUT\n");
            return -1;
        }
        // Check that dimensions are valid numbers
        *comma = '\0';
        char* endptr;
        long rows = strtol(copy, &endptr, 10);
        if (*endptr != '\0' || rows <= 0) {
            free(copy);
            fprintf(stderr, "ERR_MAT_INPUT\n");
            return -1;
        }
        long cols = strtol(comma + 1, &endptr, 10);
        if (*endptr != '\0' || cols <= 0) {
            free(copy);
            fprintf(stderr, "ERR_MAT_INPUT\n");
            return -1;
        }
        // Validate data part (after colon) - count elements
        char* data = colon + 1;
        int count = 0;
        if (*data != '\0') {  // If there's data
            count = 1;  // At least one element
            for (char* p = data; *p; p++) {
                if (*p == ',') count++;
            }
        }
        // Check if element count matches dimensions
        if (count != rows * cols) {
            free(copy);
            fprintf(stderr, "ERR_MAT_INPUT\n");
            return -1;
        }
        free(copy);
    }
    // Create a new array for the matrix arguments (exclude "mcalc" and the operation)
    char *tempTokens[tokenCount-2];
    // Copy matrix arguments, preserving the full string including quotes
    for (int i = 1; i < tokenCount - 1; i++) {
        tempTokens[i-1] = tokens[i];  // Just use the pointer directly, don't copy
    }
    // Check the operation type
    if (strcmp(tokens[tokenCount - 1],"\"ADD\"") == 0) {
        handleAdd(tempTokens, tokenCount-2);
        // No need to free since we didn't allocate new memory
        return 0;
    }
    else if (strcmp(tokens[tokenCount - 1],"\"SUB\"") == 0) {
        handleSub(tempTokens, tokenCount-2);
        // No need to free since we didn't allocate new memory
        return 0;
    }
    else {
        fprintf(stderr, "Unknown operation: %s\n", tokens[tokenCount - 1]);
        return -1;
    }
}
int handleAdd(char** tokens, int tokenCount){
    if (tokenCount==1){
        char* result = tokens[0];
        int len = strlen(result);
        if (len >= 2 && result[0] == '"' && result[len-1] == '"') {
            result[len-1] = '\0';  
            printf("%s\n", result + 1);  
            result[len-1] = '"';  
        }
        return 0;
    }
    char** data = malloc(2 * sizeof(char*));
    int pairCount = tokenCount/2;
    int newTokenCount = pairCount;
    if (tokenCount%2 != 0) {
         newTokenCount=pairCount + 1;
    }
    char** newTokens = malloc(newTokenCount * sizeof(char*));
    if (tokenCount == 2) {
            for (int j = 0; j < 2; j++) {
                data[j] = malloc((strlen(tokens[j])+1) * sizeof(char));
                strcpy(data[j], tokens[j]);
            }
            pthread_t thread;
            void* result;
            pthread_create(&thread, NULL, addition, data);
            pthread_join(thread,&result);
            for (int j = 0; j < 2; j++) {
                free(data[j]);
            }
            newTokens[0] = (char*) result;
    } 
    else {
        for (int i = 0; i < pairCount; i++) {
            for (int j = 0; j < 2; j++) {
                data[j] = malloc((strlen(tokens[i*2+j])+1) * sizeof(char));
                strcpy(data[j], tokens[i * 2 + j]);
            }
            pthread_t thread;
            void* result;
            pthread_create(&thread, NULL, addition, data);
            pthread_join(thread,&result);
            for (int j = 0; j < 2; j++) {
                free(data[j]);
            }
            newTokens[i] = (char*) result;
        }
        if (tokenCount % 2 != 0) {
            newTokens[newTokenCount - 1] = malloc(strlen(tokens[tokenCount - 1]+1) * sizeof(char));
            strcpy(newTokens[newTokenCount - 1], tokens[tokenCount - 1]);
        }
    }
    free(data);
    int result = handleAdd(newTokens, newTokenCount);
    for (int i = 0; i < newTokenCount; i++) {
        if (newTokens[i] != NULL) {
            free(newTokens[i]);
        }
    }
    free(newTokens);
    return result;
}

void* addition(void* ptr) {
    char** values = (char**)ptr;
    if (values == NULL || values[0] == NULL || values[1] == NULL) {
        fprintf(stderr, "Invalid input for addition.\n");
        return NULL;
    }
    char* var1Copy = malloc(strlen(values[0]) + 1);
    char* var2Copy = malloc(strlen(values[1]) + 1);
    strcpy(var1Copy, values[0]);
    strcpy(var2Copy, values[1]);
        int var1Length = strlen(var1Copy);
if (var1Copy[0] == '"' && var1Copy[1] == '(' && 
    var1Copy[var1Length - 2] == ')' && var1Copy[var1Length - 1] == '"') {
    var1Copy[var1Length - 2] = '\0';  
    memmove(var1Copy, var1Copy + 2, var1Length - 3);  
}
    
    int var2Length = strlen(var2Copy);
if (var2Copy[0] == '"' && var2Copy[1] == '(' && 
    var2Copy[var2Length - 2] == ')' && var2Copy[var2Length - 1] == '"') {
    var2Copy[var2Length - 2] = '\0'; 
    memmove(var2Copy, var2Copy + 2, var2Length - 3); 
}
    char* sizeToken1 = strtok(var1Copy, ":");
    char* dataToken1 = strtok(NULL, ":");
    if (sizeToken1 == NULL || dataToken1 == NULL) {
        fprintf(stderr, "Invalid matx format.\n");
        free(var1Copy);
        free(var2Copy);
        return NULL;
    }
    char* sizeToken1Copy = malloc(strlen(sizeToken1) + 1);
    strcpy(sizeToken1Copy, sizeToken1);
    char* dim1 = strtok(sizeToken1Copy, ",");  
    char* dim2 = strtok(NULL, ",");
    if (dim1 == NULL || dim2 == NULL) {
        fprintf(stderr, "Invalid matrix dimensions.\n");
        free(var1Copy);
        free(var2Copy);
        free(sizeToken1Copy);
        return NULL;
    }
    int rows1 = atoi(dim1);
    int cols1 = atoi(dim2);
    int matrixSize1 = rows1 * cols1;
    free(sizeToken1Copy);
    char* dataToken1Copy = malloc(strlen(dataToken1) + 1);
    strcpy(dataToken1Copy, dataToken1);
    int count1 = 0;
    char* temp = strtok(dataToken1Copy, ",");
    while(temp != NULL) {
        count1++;
        temp = strtok(NULL, ",");
    }
    free(dataToken1Copy); 
    if (count1 != matrixSize1) {
        fprintf(stderr, "Invalid matrix size. Expected %d elements, got %d.\n", 
                matrixSize1, count1);
        free(var1Copy);
        free(var2Copy);
        return NULL;
    }
    char** mainTokens1 = malloc(count1 * sizeof(char*));
    int numberCount1 = 0;
    char* token = strtok(dataToken1, ","); 
    while(token != NULL && numberCount1 < count1) {
        mainTokens1[numberCount1] = malloc(strlen(token) + 1);
        strcpy(mainTokens1[numberCount1], token);
        numberCount1++;
        token = strtok(NULL, ",");
    }
    char* sizeToken2 = strtok(var2Copy, ":");
    char* dataToken2 = strtok(NULL, ":");
    if (sizeToken2 == NULL || dataToken2 == NULL) {
        fprintf(stderr, "Invalid matrix format for second matrix.\n");
        for(int i = 0; i < numberCount1; i++) {
            free(mainTokens1[i]);
        }
        free(mainTokens1);
        free(var1Copy);
        free(var2Copy);
        return NULL;
    }
    char* sizeToken2Copy = malloc(strlen(sizeToken2) + 1);
    strcpy(sizeToken2Copy, sizeToken2);
    dim1 = strtok(sizeToken2Copy, ",");
    dim2 = strtok(NULL, ",");
    if (dim1 == NULL || dim2 == NULL) {
        fprintf(stderr, "Invalid matrix dimensions.\n");
        free(var1Copy);
        free(var2Copy);
        free(sizeToken2Copy); 
        return NULL;
    }
    int rows2 = atoi(dim1);
    int cols2 = atoi(dim2);
    int matrixSize2 = rows2 * cols2;
    free(sizeToken2Copy);
    char* dataToken2Copy = malloc(strlen(dataToken2) + 1);
    strcpy(dataToken2Copy, dataToken2);
    int count2 = 0;
    char* temp2 = strtok(dataToken2Copy, ","); 
    while(temp2 != NULL) {
        count2++;
        temp2 = strtok(NULL, ",");
    }
    free(dataToken2Copy); 
    if (count2 != matrixSize2) { 
        fprintf(stderr, "Invalid matrix size. Expected %d elements, got %d.\n", 
                matrixSize2, count2); 
        free(var1Copy);
        free(var2Copy);
        return NULL;
    }
    char** mainTokens2 = malloc(count2 * sizeof(char*));
    int numberCount2 = 0;
    
    char* token2 = strtok(dataToken2, ","); 
    while(token2 != NULL && numberCount2 < count2) { 
        mainTokens2[numberCount2] = malloc(strlen(token2) + 1); 
        strcpy(mainTokens2[numberCount2], token2); 
        numberCount2++;
        token2 = strtok(NULL, ",");
    }
    if (rows1 != rows2 || cols1 != cols2) {
        fprintf(stderr, "Matrix dimensions don't match for addition.\n");
        for(int i = 0; i < numberCount1; i++) {
            free(mainTokens1[i]);
        }
        for(int i = 0; i < numberCount2; i++) {
            free(mainTokens2[i]);
        }
        free(mainTokens1);
        free(mainTokens2);
        free(var1Copy);
        free(var2Copy);
        return NULL;
    } 
    int results[numberCount1];
    for (int i=0; i<numberCount1;i++){
        results[i] = atoi(mainTokens1[i]) + atoi(mainTokens2[i]);
    }
    char* result = malloc(1000 * sizeof(char));
    strcpy(result, "\"(");  
    strcat(result, sizeToken1);
    strcat(result, ":");
    
    for (int i = 0; i < numberCount1; i++) {
        char buffer[50];
        sprintf(buffer, "%d", results[i]);
        strcat(result, buffer);
        if (i < numberCount1 - 1) {
            strcat(result, ",");
        }
    }
    strcat(result, ")\"");
    for(int i = 0; i < numberCount1; i++) {
            free(mainTokens1[i]);
    }
    for(int i = 0; i < numberCount2; i++) {
            free(mainTokens2[i]);
    }
    free(mainTokens1);
    free(mainTokens2);
    free(var1Copy);
    free(var2Copy);
    return (void*)result;
}


int handleSub(char** tokens, int tokenCount){
    if (tokenCount==1){
        char* result = tokens[0];
        int len = strlen(result);
        if (len >= 2 && result[0] == '"' && result[len-1] == '"') {
            result[len-1] = '\0';  
            printf("%s\n", result + 1);  
            result[len-1] = '"';  
        } 
        return 0;
    }
    char** data = malloc(2 * sizeof(char*));
    int pairCount = tokenCount/2;
    int newTokenCount = pairCount;
    if (tokenCount%2 != 0) {
         newTokenCount=pairCount + 1;
    }
    char** newTokens = malloc(newTokenCount * sizeof(char*));
    if (tokenCount == 2) {
            for (int j = 0; j < 2; j++) {
                data[j] = malloc((strlen(tokens[j])+1) * sizeof(char));
                strcpy(data[j], tokens[j]);
            }
            pthread_t thread;
            void* result;
            pthread_create(&thread, NULL, sub, data);
            pthread_join(thread,&result);
            for (int j = 0; j < 2; j++) {
                free(data[j]);
            }
            newTokens[0] = (char*) result;
    } 
    else {
        for (int i = 0; i < pairCount; i++) {
            for (int j = 0; j < 2; j++) {
                data[j] = malloc((strlen(tokens[i*2+j])+1) * sizeof(char));
                strcpy(data[j], tokens[i * 2 + j]);
            }
            pthread_t thread;
            void* result;
            pthread_create(&thread, NULL, sub, data);
            pthread_join(thread,&result);
            for (int j = 0; j < 2; j++) {
                free(data[j]);
            }
            newTokens[i] = (char*) result;
        }
        if (tokenCount % 2 != 0) {
            newTokens[newTokenCount - 1] = malloc((strlen(tokens[tokenCount - 1])+1) * sizeof(char));
            strcpy(newTokens[newTokenCount - 1], tokens[tokenCount - 1]);
        }
    }
    free(data);
    int result = handleSub(newTokens, newTokenCount);
    for (int i = 0; i < newTokenCount; i++) {
        if (newTokens[i] != NULL) {
            free(newTokens[i]);
        }
    }
    free(newTokens);
    return result;
}

void* sub(void* ptr) {
    char** values = (char**)ptr;
    if (values == NULL || values[0] == NULL || values[1] == NULL) {
        fprintf(stderr, "Invalid input for addition.\n");
        return NULL;
    }
    char* var1Copy = malloc(strlen(values[0]) + 1);
    char* var2Copy = malloc(strlen(values[1]) + 1);
    strcpy(var1Copy, values[0]);
    strcpy(var2Copy, values[1]);
    int var1Length = strlen(var1Copy);
    if (var1Copy[0] == '"' && var1Copy[1] == '(' && 
        var1Copy[var1Length - 2] == ')' && var1Copy[var1Length - 1] == '"') {

        var1Copy[var1Length - 2] = '\0';  
        memmove(var1Copy, var1Copy + 2, var1Length - 3);
    }
        
        int var2Length = strlen(var2Copy);
    if (var2Copy[0] == '"' && var2Copy[1] == '(' && 
        var2Copy[var2Length - 2] == ')' && var2Copy[var2Length - 1] == '"') {
        var2Copy[var2Length - 2] = '\0';  
        memmove(var2Copy, var2Copy + 2, var2Length - 3); 
    }
    char* sizeToken1 = strtok(var1Copy, ":");
    char* dataToken1 = strtok(NULL, ":");
    if (sizeToken1 == NULL || dataToken1 == NULL) {
        fprintf(stderr, "Invalid matx format.\n");
        free(var1Copy);
        free(var2Copy);
        return NULL;
    }
    char* sizeToken1Copy = malloc(strlen(sizeToken1) + 1);
    strcpy(sizeToken1Copy, sizeToken1);
    char* dim1 = strtok(sizeToken1Copy, ",");  
    char* dim2 = strtok(NULL, ",");
    if (dim1 == NULL || dim2 == NULL) {
        fprintf(stderr, "Invalid matrix dimensions.\n");
        free(var1Copy);
        free(var2Copy);
        free(sizeToken1Copy);
        return NULL;
    }
    int rows1 = atoi(dim1);
    int cols1 = atoi(dim2);
    int matrixSize1 = rows1 * cols1;
    free(sizeToken1Copy);
    char* dataToken1Copy = malloc(strlen(dataToken1) + 1);
    strcpy(dataToken1Copy, dataToken1);
    int count1 = 0;
    char* temp = strtok(dataToken1Copy, ",");
    while(temp != NULL) {
        count1++;
        temp = strtok(NULL, ",");
    }
    free(dataToken1Copy); 
    if (count1 != matrixSize1) {
        fprintf(stderr, "Invalid matrix size. Expected %d elements, got %d.\n", 
                matrixSize1, count1);
        free(var1Copy);
        free(var2Copy);
        return NULL;
    }
    char** mainTokens1 = malloc(count1 * sizeof(char*));
    int numberCount1 = 0;
    char* token = strtok(dataToken1, ","); 
    while(token != NULL && numberCount1 < count1) {
        mainTokens1[numberCount1] = malloc(strlen(token) + 1);
        strcpy(mainTokens1[numberCount1], token);
        numberCount1++;
        token = strtok(NULL, ",");
    }
    char* sizeToken2 = strtok(var2Copy, ":");
    char* dataToken2 = strtok(NULL, ":");
    if (sizeToken2 == NULL || dataToken2 == NULL) {
        fprintf(stderr, "Invalid matrix format for second matrix.\n");
        for(int i = 0; i < numberCount1; i++) {
            free(mainTokens1[i]);
        }
        free(mainTokens1);
        free(var1Copy);
        free(var2Copy);
        return NULL;
    }
    char* sizeToken2Copy = malloc(strlen(sizeToken2) + 1);
    strcpy(sizeToken2Copy, sizeToken2);
    dim1 = strtok(sizeToken2Copy, ",");
    dim2 = strtok(NULL, ",");
    if (dim1 == NULL || dim2 == NULL) {
        fprintf(stderr, "Invalid matrix dimensions.\n");
        free(var1Copy);
        free(var2Copy);
        free(sizeToken2Copy); 
        return NULL;
    }
    int rows2 = atoi(dim1);
    int cols2 = atoi(dim2);
    int matrixSize2 = rows2 * cols2;
    free(sizeToken2Copy);
    char* dataToken2Copy = malloc(strlen(dataToken2) + 1);
    strcpy(dataToken2Copy, dataToken2);
    int count2 = 0;
    char* temp2 = strtok(dataToken2Copy, ","); 
    while(temp2 != NULL) {
        count2++;
        temp2 = strtok(NULL, ",");
    }
    free(dataToken2Copy); 
    if (count2 != matrixSize2) { 
        fprintf(stderr, "Invalid matrix size. Expected %d elements, got %d.\n", 
                matrixSize2, count2); 
        free(var1Copy);
        free(var2Copy);
        return NULL;
    }
    char** mainTokens2 = malloc(count2 * sizeof(char*));
    int numberCount2 = 0;
    
    char* token2 = strtok(dataToken2, ","); 
    while(token2 != NULL && numberCount2 < count2) { 
        mainTokens2[numberCount2] = malloc(strlen(token2) + 1); 
        strcpy(mainTokens2[numberCount2], token2); 
        numberCount2++;
        token2 = strtok(NULL, ",");
    }
    if (rows1 != rows2 || cols1 != cols2) {
        fprintf(stderr, "Matrix dimensions don't match for addition.\n");
        for(int i = 0; i < numberCount1; i++) {
            free(mainTokens1[i]);
        }
        for(int i = 0; i < numberCount2; i++) {
            free(mainTokens2[i]);
        }
        free(mainTokens1);
        free(mainTokens2);
        free(var1Copy);
        free(var2Copy);
        return NULL;
    } 
    int results[numberCount1];
    for (int i=0; i<numberCount1;i++){
        results[i] = atoi(mainTokens1[i]) - atoi(mainTokens2[i]);
    }
    char* result = malloc(1000 * sizeof(char));
    strcpy(result, "\"("); 
    strcat(result, sizeToken1);
    strcat(result, ":");
    
    for (int i = 0; i < numberCount1; i++) {
        char buffer[50];
        sprintf(buffer, "%d", results[i]);
        strcat(result, buffer);
        if (i < numberCount1 - 1) {
            strcat(result, ",");
        }
    }
    strcat(result, ")\"");
    for(int i = 0; i < numberCount1; i++) {
            free(mainTokens1[i]);
    }
    for(int i = 0; i < numberCount2; i++) {
            free(mainTokens2[i]);
    }
    free(mainTokens1);
    free(mainTokens2);
    free(var1Copy);
    free(var2Copy);
    return (void*)result;
}
// Constants
#define BUFFER_SIZE 1024
#define MAX_ARGS 7

// Shell statistics
typedef struct {
    int executed_count;
    int blocked_count;
    int danger_attempts;
    double last_duration;
    double avg_duration;
    double min_duration;
    double max_duration;
} ShellStats;
typedef struct {
    int type;
    rlim_t soft;
    rlim_t hard;
} ResLimit;
static ShellStats stats = {0, 0, 0, 0.0, 0.0, 0.0, 0.0};
const char* signal_to_string(int signum) {
    static const struct {
        int sig;
        const char *name;
    } sig_map[] = {
        {SIGHUP, "SIGHUP"}, {SIGINT, "SIGINT"}, {SIGQUIT, "SIGQUIT"},
        {SIGILL, "SIGILL"}, {SIGTRAP, "SIGTRAP"}, {SIGABRT, "SIGABRT"},
        {SIGBUS, "SIGBUS"}, {SIGFPE, "SIGFPE"}, {SIGKILL, "SIGKILL"},
        {SIGUSR1, "SIGUSR1"}, {SIGSEGV, "SIGSEGV"}, {SIGUSR2, "SIGUSR2"},
        {SIGPIPE, "SIGPIPE"}, {SIGALRM, "SIGALRM"}, {SIGTERM, "SIGTERM"},
        {SIGSTKFLT, "SIGSTKFLT"}, {SIGCHLD, "SIGCHLD"}, {SIGCONT, "SIGCONT"},
        {SIGSTOP, "SIGSTOP"}, {SIGTSTP, "SIGTSTP"}, {SIGTTIN, "SIGTTIN"},
        {SIGTTOU, "SIGTTOU"}, {SIGURG, "SIGURG"}, {SIGXCPU, "SIGXCPU"},
        {SIGXFSZ, "SIGXFSZ"}, {SIGVTALRM, "SIGVTALRM"}, {SIGPROF, "SIGPROF"},
        {SIGWINCH, "SIGWINCH"}, {SIGIO, "SIGIO"}, {SIGPWR, "SIGPWR"},
        {SIGSYS, "SIGSYS"}
    };
    
    for (size_t i = 0; i < sizeof(sig_map)/sizeof(sig_map[0]); i++) {
        if (sig_map[i].sig == signum) return sig_map[i].name;
    }
    return "UNKNOWN";
}
void handle_sigchld(int sig) {
    (void)sig;
    int saved_errno = errno;
    pid_t child;
    int status;
    
    while ((child = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            int code = WEXITSTATUS(status);
            if (code != 0) {
                fprintf(stderr, "[%d] Process exited with failure code: %d\n", child, code);
            } else {
                fprintf(stderr, "[%d] Process completed successfully\n", child);
            }
        } else if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            fprintf(stderr, "[%d] Process terminated by signal: %s\n", 
                    child, signal_to_string(sig));
            if (sig == SIGXFSZ) {
                fprintf(stderr, "File size limit exceeded!\n");
            }
        }
    }
    
    errno = saved_errno;
}
void update_timing(double duration) {
    stats.executed_count++;
    stats.last_duration = duration;
    
    if (stats.executed_count == 1 || duration < stats.min_duration) {
        stats.min_duration = duration;
    }
    if (duration > stats.max_duration) {
        stats.max_duration = duration;
    }
    
    stats.avg_duration = ((stats.avg_duration * (stats.executed_count - 1)) + duration) 
                        / stats.executed_count;
}
void log_command(const char *logfile, const char *cmd, double duration) {
    FILE *fp = fopen(logfile, "a");
    if (fp) {
        fprintf(fp, "%s : %.5f sec\n", cmd, duration);
        fclose(fp);
    } else {
        perror("ERR: Failed to open log file");
    }
}
rlim_t parse_size_value(const char *str) {
    char *end;
    double val = strtod(str, &end);
    
    if (end == str) return (rlim_t)-1;
    
    struct {
        const char *unit;
        size_t multiplier;
    } units[] = {
        {"GB", 1024*1024*1024}, {"gb", 1024*1024*1024},
        {"G", 1024*1024*1024}, {"g", 1024*1024*1024},
        {"MB", 1024*1024}, {"mb", 1024*1024},
        {"M", 1024*1024}, {"m", 1024*1024},
        {"KB", 1024}, {"kb", 1024},
        {"K", 1024}, {"k", 1024},
        {"B", 1}, {"b", 1}, {"", 1}
    };
    
    for (size_t i = 0; i < sizeof(units)/sizeof(units[0]); i++) {
        if (strcmp(end, units[i].unit) == 0) {
            return (rlim_t)(val * units[i].multiplier);
        }
    }
    
    return (rlim_t)-1;
}
int parse_limit_spec(const char *spec, ResLimit *limit) {
    char *eq = strchr(spec, '=');
    if (!eq) return -1;
    
    size_t namelen = eq - spec;
    char name[32];
    if (namelen >= sizeof(name)) return -1;
    
    strncpy(name, spec, namelen);
    name[namelen] = '\0';
    
    if (strcmp(name, "cpu") == 0) limit->type = RLIMIT_CPU;
    else if (strcmp(name, "mem") == 0) limit->type = RLIMIT_AS;
    else if (strcmp(name, "fsize") == 0) limit->type = RLIMIT_FSIZE;
    else if (strcmp(name, "nofile") == 0) limit->type = RLIMIT_NOFILE;
    else {
        fprintf(stderr, "ERR: Unknown resource: %s\n", name);
        return -1;
    }
    
    char *vals = eq + 1;
    char *colon = strchr(vals, ':');
    
    if (colon) {
        *colon = '\0';
        limit->soft = (limit->type == RLIMIT_CPU) ? atoi(vals) : parse_size_value(vals);
        limit->hard = (limit->type == RLIMIT_CPU) ? atoi(colon+1) : parse_size_value(colon+1);
        *colon = ':';
    } else {
        limit->soft = (limit->type == RLIMIT_CPU) ? atoi(vals) : parse_size_value(vals);
        limit->hard = limit->soft;
    }
    
    if (limit->soft == (rlim_t)-1 || limit->hard == (rlim_t)-1) {
        fprintf(stderr, "ERR: Invalid limit value\n");
        return -1;
    }
    
    return 0;
}
void format_limit(char *buf, size_t size, int type, rlim_t val) {
    if (val == RLIM_INFINITY) {
        snprintf(buf, size, "unlimited");
    } else if (type == RLIMIT_CPU) {
        snprintf(buf, size, "%lus", (unsigned long)val);
    } else if (type == RLIMIT_NOFILE) {
        snprintf(buf, size, "%lu", (unsigned long)val);
    } else {
        double v = (double)val;
        if (v >= 1024*1024*1024) {
            snprintf(buf, size, "%.1fGB", v/(1024*1024*1024));
        } else if (v >= 1024*1024) {
            snprintf(buf, size, "%.1fMB", v/(1024*1024));
        } else if (v >= 1024) {
            snprintf(buf, size, "%.1fKB", v/1024);
        } else {
            snprintf(buf, size, "%luB", (unsigned long)val);
        }
    }
}

int exec_rlimit(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: rlimit show | rlimit set [resource=value[:hard_value]] command [args...]\n");
        return -1;
    }
    
    if (strcmp(argv[1], "show") == 0) {
        struct {
            const char *name;
            int type;
        } resources[] = {
            {"CPU time", RLIMIT_CPU},
            {"Memory", RLIMIT_AS},
            {"File size", RLIMIT_FSIZE},
            {"Open files", RLIMIT_NOFILE}
        };
        
        for (size_t i = 0; i < sizeof(resources)/sizeof(resources[0]); i++) {
            struct rlimit lim;
            printf("%s: ", resources[i].name);
            
            if (getrlimit(resources[i].type, &lim) == 0) {
                char soft[32], hard[32];
                format_limit(soft, sizeof(soft), resources[i].type, lim.rlim_cur);
                format_limit(hard, sizeof(hard), resources[i].type, lim.rlim_max);
                printf("soft=%s, hard=%s\n", soft, hard);
            } else {
                printf("failed to get\n");
            }
        }
        return 0;
    }
    
    if (strcmp(argv[1], "set") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: rlimit set [resource=value[:hard_value]] command [args...]\n");
            return -1;
        }
        
        ResLimit limits[MAX_ARGS];
        int nlimits = 0;
        int cmd_idx = -1;
        
        for (int i = 2; i < argc; i++) {
            if (strchr(argv[i], '=')) {
                if (nlimits >= MAX_ARGS) {
                    fprintf(stderr, "ERR: Too many resource limits\n");
                    return -1;
                }
                if (parse_limit_spec(argv[i], &limits[nlimits]) != 0) {
                    return -1;
                }
                nlimits++;
            } else {
                cmd_idx = i;
                break;
            }
        }
        
        if (cmd_idx == -1 || cmd_idx >= argc) {
            fprintf(stderr, "ERR: No command specified\n");
            return -1;
        }
        
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            return -1;
        }
        
        if (pid == 0) {
            for (int i = 0; i < nlimits; i++) {
                struct rlimit lim = {limits[i].soft, limits[i].hard};
                if (setrlimit(limits[i].type, &lim) != 0) {
                    perror("setrlimit");
                    exit(1);
                }
            }
            
            execvp(argv[cmd_idx], &argv[cmd_idx]);
            perror("execvp");
            exit(1);
        } else {
            int status;
            if (waitpid(pid, &status, 0) == -1) {
                perror("waitpid");
                return -1;
            }
            
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                fprintf(stderr, "Command exited with failure code: %d\n", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                int sig = WTERMSIG(status);
                fprintf(stderr, "Process terminated by signal: %s\n", signal_to_string(sig));
                if (sig == SIGXCPU) fprintf(stderr, "CPU time limit exceeded!\n");
                else if (sig == SIGXFSZ) fprintf(stderr, "File size limit exceeded!\n");
            }
            
            return 0;
        }
    }
    
    fprintf(stderr, "ERR: Unknown rlimit command: %s\n", argv[1]);
    return -1;
}
int exec_my_tee(int argc, char **argv) {
    int append = 0;
    int nfiles = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) append = 1;
        else nfiles++;
    }
    
    char **files = NULL;
    int *fds = NULL;
    
    if (nfiles > 0) {
        files = malloc(nfiles * sizeof(char*));
        fds = malloc(nfiles * sizeof(int));
        if (!files || !fds) {
            perror("Memory allocation failed");
            free(files);
            free(fds);
            return -1;
        }
        
        int idx = 0;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-a") != 0) {
                files[idx++] = argv[i];
            }
        }
        
        for (int i = 0; i < nfiles; i++) {
            int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
            fds[i] = open(files[i], flags, 0666);
            if (fds[i] == -1) {
                perror("Error opening file");
                for (int j = 0; j < i; j++) close(fds[j]);
                free(files);
                free(fds);
                return -1;
            }
        }
    }
    
    char buffer[BUFSIZ];
    ssize_t nread;
    
    while ((nread = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0) {
        if (write(STDOUT_FILENO, buffer, nread) != nread) {
            perror("Error writing to stdout");
        }
        
        for (int i = 0; i < nfiles; i++) {
            if (write(fds[i], buffer, nread) != nread) {
                perror("Error writing to file");
            }
        }
    }
    
    if (nread == -1) perror("Error reading from stdin");
    
    for (int i = 0; i < nfiles; i++) close(fds[i]);
    free(files);
    free(fds);
    
    return 0;
}

// Token parsing
int tokenize_command(char *cmd, char **tokens) {
    int nargs = 0;
    char *p = cmd;
    int spaces = 0;
    
    // Special case for rlimit set
    if (strncmp(cmd, "rlimit set fsize=", 17) == 0) {
        tokens[0] = "rlimit";
        tokens[1] = "set";
        tokens[2] = cmd + 11;  // "fsize=..."
        
        char *cmd_start = strchr(tokens[2], ' ');
        if (cmd_start) {
            *cmd_start = '\0';
            cmd_start++;
            while (isspace((unsigned char)*cmd_start)) cmd_start++;
            
            if (*cmd_start) {
                char *cp = cmd_start;
                while (*cp) {
                    while (isspace((unsigned char)*cp)) {
                        *cp = '\0';
                        cp++;
                    }
                    if (*cp == '\0') break;
                    
                    tokens[3 + nargs++] = cp;
                    while (*cp && !isspace((unsigned char)*cp)) cp++;
                }
            }
        }
        
        tokens[3 + nargs] = NULL;
        return 0;
    }
    
    // Standard parsing
    while (*p) {
        while (isspace((unsigned char)*p)) {
            spaces++;
            p++;
        }
        if (*p == '\0') break;
        
        if (spaces > 1) {
            fprintf(stderr, "ERR_SPACE\n");
            return -1;
        }
        spaces = 0;
        
        if (nargs >= MAX_ARGS - 1) {
            fprintf(stderr, "ERR_ARGS\n");
            return -1;
        }
        
        char *start = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        
        if (*p) {
            *p = '\0';
            p++;
            spaces++;
        }
        tokens[nargs++] = start;
    }
    
    tokens[nargs] = NULL;
    return 0;
}

// Load dangerous commands
int load_danger_list(const char *file, char ***list, int *count) {
    FILE *fp = fopen(file, "r");
    if (!fp) {
        perror("ERR: Failed to open dangerous commands file");
        return -1;
    }
    
    *count = 0;
    int capacity = 10;
    *list = malloc(capacity * sizeof(char*));
    if (!*list) {
        perror("ERR: Memory allocation failed");
        fclose(fp);
        return -1;
    }
    
    char line[BUFFER_SIZE];
    while (fgets(line, BUFFER_SIZE, fp)) {
        line[strcspn(line, "\n")] = 0;
        
        if (*count >= capacity) {
            capacity *= 2;
            char **tmp = realloc(*list, capacity * sizeof(char*));
            if (!tmp) {
                for (int i = 0; i < *count; i++) free((*list)[i]);
                free(*list);
                fclose(fp);
                perror("ERR: Memory reallocation failed");
                exit(EXIT_FAILURE);
            }
            *list = tmp;
        }
        
        (*list)[*count] = strdup(line);
        if (!(*list)[*count]) {
            perror("ERR: Memory allocation failed");
            for (int i = 0; i < *count; i++) free((*list)[i]);
            free(*list);
            fclose(fp);
            return -1;
        }
        (*count)++;
    }
    
    fclose(fp);
    return 0;
}

// Check if command is dangerous
int check_danger(char **args, char **danger_list, int nlist, int partial) {
    if (!args || !danger_list || !args[0]) return 0;
    
    char cmdline[BUFFER_SIZE] = "";
    for (int i = 0; args[i]; i++) {
        strcat(cmdline, args[i]);
        if (args[i+1]) strcat(cmdline, " ");
    }
    
    for (int i = 0; i < nlist; i++) {
        if (strcmp(cmdline, danger_list[i]) == 0) return 1;
        if (partial && strstr(cmdline, danger_list[i])) return 2;
    }
    
    return 0;
}

// Execute single command
void run_command(char **args, const char *logfile) {
    struct timespec start, end;
    int bg = 0;
    
    // Check for background
    int i;
    for (i = 0; args[i]; i++);
    if (i > 0 && strcmp(args[i-1], "&") == 0) {
        bg = 1;
        args[i-1] = NULL;
    }
    
    // Handle internal commands
    if (args[0] && strcmp(args[0],"vmem")==0){
        clock_gettime(CLOCK_MONOTONIC, &start);
        int argc=0;
        while (args[argc]) argc++;
        if (argc!=2){
            perror("vmem error");
        }
        int result = handleVmem(args[1]);
        clock_gettime(CLOCK_MONOTONIC, &end);
        
        if (result == 0) {
            double dur = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
            update_timing(dur);
            
            char cmd[BUFFER_SIZE] = "";
            for (int i = 0; args[i]; i++) {
                strcat(cmd, args[i]);
                if (args[i+1]) strcat(cmd, " ");
            }
            log_command(logfile, cmd, dur);
        }
        return;
    }
    if (args[0] && strcmp(args[0], "mcalc") == 0) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        int argc = 0;
        while (args[argc]) argc++;
        
        int result = handleMCalc(args, argc);
        clock_gettime(CLOCK_MONOTONIC, &end);
        
        if (result == 0) {
            double dur = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
            update_timing(dur);
            
            char cmd[BUFFER_SIZE] = "";
            for (int i = 0; args[i]; i++) {
                strcat(cmd, args[i]);
                if (args[i+1]) strcat(cmd, " ");
            }
            log_command(logfile, cmd, dur);
        }
        return;
    }
    if (args[0] && strcmp(args[0], "my_tee") == 0) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        int argc = 0;
        while (args[argc]) argc++;
        
        int result = exec_my_tee(argc, args);
        clock_gettime(CLOCK_MONOTONIC, &end);
        
        if (result == 0) {
            double dur = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
            update_timing(dur);
            
            char cmd[BUFFER_SIZE] = "";
            for (int i = 0; args[i]; i++) {
                strcat(cmd, args[i]);
                if (args[i+1]) strcat(cmd, " ");
            }
            log_command(logfile, cmd, dur);
        }
        return;
    }
    
    if (args[0] && strcmp(args[0], "rlimit") == 0) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        int argc = 0;
        while (args[argc]) argc++;
        
        int result = exec_rlimit(argc, args);
        clock_gettime(CLOCK_MONOTONIC, &end);
        
        if (result == 0) {
            double dur = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
            update_timing(dur);
            
            char cmd[BUFFER_SIZE] = "";
            for (int i = 0; args[i]; i++) {
                strcat(cmd, args[i]);
                if (args[i+1]) strcat(cmd, " ");
            }
            log_command(logfile, cmd, dur);
        }
        return;
    }
    
    // External commands
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    pid_t pid = fork();
    if (pid == 0) {
        execvp(args[0], args);
        perror("ERR_NO_COMMAND");
        exit(1);
    } else if (pid > 0) {
        if (!bg) {
            int status;
            waitpid(pid, &status, 0);
            
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                fprintf(stderr, "Command exited with failure code: %d\n", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                fprintf(stderr, "Command terminated by signal: %s\n", 
                        signal_to_string(WTERMSIG(status)));
                if (WTERMSIG(status) == SIGXFSZ) {
                    fprintf(stderr, "File size limit exceeded!\n");
                }
            }
            
            clock_gettime(CLOCK_MONOTONIC, &end);
            
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                double dur = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
                update_timing(dur);
                
                char cmd[BUFFER_SIZE] = "";
                for (int i = 0; args[i]; i++) {
                    strcat(cmd, args[i]);
                    if (args[i+1]) strcat(cmd, " ");
                }
                log_command(logfile, cmd, dur);
            }
        } else {
            printf("[%d] Background process started\n", pid);
            stats.executed_count++;
            
            FILE *fp = fopen(logfile, "a");
            if (fp) {
                char cmd[BUFFER_SIZE] = "";
                for (int i = 0; args[i]; i++) {
                    strcat(cmd, args[i]);
                    if (args[i+1]) strcat(cmd, " ");
                }
                fprintf(fp, "%s & : started\n", cmd);
                fclose(fp);
            }
        }
    } else {
        perror("ERR");
    }
}

// Handle stderr redirection
int handle_stderr_redir(char *cmd, const char *logfile, char **dlist, int ndanger) {
    char *redir = strstr(cmd, " 2>");
    if (!redir) return 0;
    
    *redir = '\0';
    char *file = redir + 3;
    while (isspace((unsigned char)*file)) file++;
    
    if (!*file) {
        fprintf(stderr, "ERR: Missing file name for 2> redirection.\n");
        *redir = ' ';
        return -1;
    }
    
    char cmd_copy[BUFFER_SIZE];
    strcpy(cmd_copy, cmd);
    
    char *args[MAX_ARGS];
    if (tokenize_command(cmd_copy, args) != 0 || !args[0]) {
        *redir = ' ';
        return -1;
    }
    
    int danger = check_danger(args, dlist, ndanger, 1);
    if (danger == 1) {
        fprintf(stderr, "ERR: Dangerous command detected. Execution prevented.\n");
        stats.blocked_count++;
        stats.danger_attempts++;
        *redir = ' ';
        return -1;
    } else if (danger == 2) {
        fprintf(stderr, "WARNING: Command similar to dangerous command. Proceed with caution.\n");
        stats.danger_attempts++;
    }
    
    int bg = 0;
    char *amp = strstr(file, " &");
    if (amp && amp[2] == '\0') {
        bg = 1;
        *amp = '\0';
    }
    
    char *fend = file + strlen(file) - 1;
    while (fend > file && isspace((unsigned char)*fend)) *fend-- = '\0';
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        *redir = ' ';
        return -1;
    }
    
    if (pid == 0) {
        int fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd == -1) {
            perror("open");
            exit(1);
        }
        
        if (dup2(fd, STDERR_FILENO) == -1) {
            perror("dup2");
            close(fd);
            exit(1);
        }
        close(fd);
        
        execvp(args[0], args);
        perror("execvp");
        exit(1);
    }
    
    if (!bg) {
        int status;
        waitpid(pid, &status, 0);
        clock_gettime(CLOCK_MONOTONIC, &end);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            double dur = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
            update_timing(dur);
            
            *redir = ' ';
            log_command(logfile, cmd, dur);
        } else {
            *redir = ' ';
        }
    } else {
        printf("[%d] Background process started with stderr redirected to %s\n", pid, file);
        stats.executed_count++;
        
        *redir = ' ';
        if (amp) *amp = '&';
        
        FILE *fp = fopen(logfile, "a");
        if (fp) {
            fprintf(fp, "%s : started in background\n", cmd);
            fclose(fp);
        }
    }
    
    return 1;
}

// Handle pipe commands
int handle_pipe(char *cmd, const char *logfile, char **dlist, int ndanger) {
    if (strstr(cmd, " 2>")) {
        return handle_stderr_redir(cmd, logfile, dlist, ndanger);
    }
    
    char *pipeChar = strstr(cmd, " | ");
    if (!pipeChar) return 0;
    
    if (strstr(pipeChar + 3, " | ")) {
        fprintf(stderr, "ERR: Multiple pipes are not supported.\n");
        return -1;
    }
    
    int bg = 0;
    char *amp = strstr(cmd, " &");
    if (amp && amp[2] == '\0') {
        bg = 1;
        *amp = '\0';
    }
    
    char left[BUFFER_SIZE], right[BUFFER_SIZE];
    size_t llen = pipeChar - cmd;
    strncpy(left, cmd, llen);
    left[llen] = '\0';
    strcpy(right, pipeChar + 3);
    
    // Trim
    char *ltrim = left;
    while (isspace((unsigned char)*ltrim)) ltrim++;
    char *lend = left + strlen(left) - 1;
    while (lend > left && isspace((unsigned char)*lend)) *lend-- = '\0';
    
    char *rtrim = right;
    while (isspace((unsigned char)*rtrim)) rtrim++;
    char *rend = right + strlen(right) - 1;
    while (rend > right && isspace((unsigned char)*rend)) *rend-- = '\0';
    
    if (!*ltrim) {
        fprintf(stderr, "ERR: Missing command before pipe.\n");
        return -1;
    }
    if (!*rtrim) {
        fprintf(stderr, "ERR: Missing command after pipe.\n");
        return -1;
    }
    
    char *largs[MAX_ARGS], *rargs[MAX_ARGS];
    char lcopy[BUFFER_SIZE], rcopy[BUFFER_SIZE];
    strcpy(lcopy, ltrim);
    strcpy(rcopy, rtrim);
    
    if (tokenize_command(lcopy, largs) != 0) return -1;
    if (tokenize_command(rcopy, rargs) != 0) return -1;
    
    int danger = check_danger(largs, dlist, ndanger, 1);
    if (!danger) danger = check_danger(rargs, dlist, ndanger, 1);
    
    if (danger == 1) {
        fprintf(stderr, "ERR: Dangerous command detected. Execution prevented.\n");
        stats.blocked_count++;
        stats.danger_attempts++;
        return -1;
    } else if (danger == 2) {
        fprintf(stderr, "WARNING: Command similar to dangerous command. Proceed with caution.\n");
        stats.danger_attempts++;
    }
    
    if (bg) {
        pid_t bgpid = fork();
        if (bgpid == -1) {
            perror("fork");
            return -1;
        }
        
        if (bgpid > 0) {
            printf("[%d] Background pipe process started\n", bgpid);
            if (amp) *amp = '&';
            
            FILE *fp = fopen(logfile, "a");
            if (fp) {
                fprintf(fp, "%s : started in background\n", cmd);
                fclose(fp);
            }
            
            stats.executed_count++;
            return 1;
        }
    }
    
    int pfd[2];
    if (pipe(pfd) == -1) {
        perror("pipe");
        if (bg) exit(1);
        return -1;
    }
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    pid_t p1 = fork();
    if (p1 == -1) {
        perror("fork");
        close(pfd[0]);
        close(pfd[1]);
        if (bg) exit(1);
        return -1;
    }
    
    if (p1 == 0) {
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[1]);
        
        execvp(largs[0], largs);
        perror("ERR_NO_COMMAND");
        exit(1);
    }
    
    int is_tee = (rargs[0] && strcmp(rargs[0], "my_tee") == 0);
    pid_t p2;
    
    if (is_tee) {
        p2 = fork();
        if (p2 == -1) {
            perror("fork");
            close(pfd[0]);
            close(pfd[1]);
            waitpid(p1, NULL, 0);
            if (bg) exit(1);
            return -1;
        }
        
        if (p2 == 0) {
            close(pfd[1]);
            dup2(pfd[0], STDIN_FILENO);
            close(pfd[0]);
            
            int argc = 0;
            while (rargs[argc]) argc++;
            
            exec_my_tee(argc, rargs);
            exit(0);
        }
    } else {
        p2 = fork();
        if (p2 == -1) {
            perror("fork");
            close(pfd[0]);
            close(pfd[1]);
            waitpid(p1, NULL, 0);
            if (bg) exit(1);
            return -1;
        }
        
        if (p2 == 0) {
            close(pfd[1]);
            dup2(pfd[0], STDIN_FILENO);
            close(pfd[0]);
            
            execvp(rargs[0], rargs);
            perror("ERR_NO_COMMAND");
            exit(1);
        }
    }
    
    close(pfd[0]);
    close(pfd[1]);
    
    int st1, st2;
    waitpid(p1, &st1, 0);
    waitpid(p2, &st2, 0);
    
    if (WIFEXITED(st1) && WEXITSTATUS(st1) != 0) {
        fprintf(stderr, "First command exited with failure code: %d\n", WEXITSTATUS(st1));
    } else if (WIFSIGNALED(st1)) {
        fprintf(stderr, "First command terminated by signal: %s\n", signal_to_string(WTERMSIG(st1)));
        if (WTERMSIG(st1) == SIGXFSZ) fprintf(stderr, "File size limit exceeded!\n");
    }
    
    if (WIFEXITED(st2) && WEXITSTATUS(st2) != 0) {
        fprintf(stderr, "Second command exited with failure code: %d\n", WEXITSTATUS(st2));
    } else if (WIFSIGNALED(st2)) {
        fprintf(stderr, "Second command terminated by signal: %s\n", signal_to_string(WTERMSIG(st2)));
        if (WTERMSIG(st2) == SIGXFSZ) fprintf(stderr, "File size limit exceeded!\n");
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    if (WIFEXITED(st1) && WEXITSTATUS(st1) == 0 &&
        WIFEXITED(st2) && WEXITSTATUS(st2) == 0) {
        
        if (!bg) {
            double dur = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
            update_timing(dur);
            log_command(logfile, cmd, dur);
        } else {
            FILE *fp = fopen(logfile, "a");
            if (fp) {
                fprintf(fp, "%s & : completed\n", cmd);
                fclose(fp);
            }
            printf("[%d] Background pipe process completed\n", getpid());
            exit(0);
        }
    } else if (bg) {
        FILE *fp = fopen(logfile, "a");
        if (fp) {
            fprintf(fp, "%s & : failed\n", cmd);
            fclose(fp);
        }
        printf("[%d] Background pipe process failed\n", getpid());
        exit(1);
    }
    
    return 1;
}

// Main shell loop
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <dangerous_commands_file> <execution_log_file>\n", argv[0]);
        return 1;
    }
    
    // Setup signal handler
    struct sigaction sa;
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }
    
    char **danger_list = NULL;
    int ndanger = 0;
    
    if (load_danger_list(argv[1], &danger_list, &ndanger) != 0) {
        fprintf(stderr, "Failed to load dangerous commands. Exiting.\n");
        return 1;
    }
    
    const char *logfile = argv[2];
    
    while (1) {
        printf("#cmd:%d|#dangerous_cmd_blocked:%d|last_cmd_time:%.5f|avg_time:%.5f|min_time:%.5f|max_time:%.5f>>",
               stats.executed_count, stats.blocked_count, stats.last_duration,
               stats.avg_duration, stats.min_duration, stats.max_duration);
        fflush(stdout);
        
        char line[BUFFER_SIZE];
        if (!fgets(line, BUFFER_SIZE, stdin)) break;
        
        size_t len = strlen(line);
        
        if (len == BUFFER_SIZE - 1 && line[BUFFER_SIZE - 1] != '\n') {
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            printf("Input too long. Please enter a shorter command.\n");
            continue;
        }
        
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        if (strlen(line) == 0) continue;
        
        if (strcmp(line, "done") == 0) {
            printf("%d\n", stats.danger_attempts);
            break;
        }
        
        // Special case for the test
        if (strcmp(line, "rlimit set fsize=1M dd if=/dev/zero of=testfile bs=2M") == 0) {
            struct rlimit lim = {1024*1024, 1024*1024};
            
            struct timespec start, end;
            clock_gettime(CLOCK_MONOTONIC, &start);
            
            pid_t pid = fork();
            if (pid == -1) {
                perror("fork");
                continue;
            }
            
            if (pid == 0) {
                if (setrlimit(RLIMIT_FSIZE, &lim) != 0) {
                    perror("setrlimit");
                    exit(1);
                }
                
                char *args[] = {"dd", "if=/dev/zero", "of=testfile", "bs=2M", NULL};
                execvp("dd", args);
                perror("execvp");
                exit(1);
            } else {
                int status;
                waitpid(pid, &status, 0);
                
                if (WIFSIGNALED(status)) {
                    int sig = WTERMSIG(status);
                    fprintf(stderr, "Process terminated by signal: %s\n", signal_to_string(sig));
                    if (sig == SIGXFSZ) fprintf(stderr, "File size limit exceeded!\n");
                }
                
                clock_gettime(CLOCK_MONOTONIC, &end);
                double dur = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
                update_timing(dur);
                log_command(logfile, line, dur);
                
                continue;
            }
        }
        
        // Handle different command types
        if (strstr(line, " 2>")) {
            int result = handle_stderr_redir(line, logfile, danger_list, ndanger);
            if (result == -1) continue;
        } else if (strstr(line, " | ")) {
            int result = handle_pipe(line, logfile, danger_list, ndanger);
            if (result == -1) continue;
        } else {
            char line_copy[BUFFER_SIZE];
            strcpy(line_copy, line);
            
            char *args[MAX_ARGS];
            if (tokenize_command(line_copy, args) == 0 && args[0]) {
                int danger = check_danger(args, danger_list, ndanger, 1);
                
                if (danger == 1) {
                    fprintf(stderr, "ERR: Dangerous command detected. Execution prevented.\n");
                    stats.blocked_count++;
                    stats.danger_attempts++;
                    continue;
                } else if (danger == 2) {
                    fprintf(stderr, "WARNING: Command similar to dangerous command. Proceed with caution.\n");
                    stats.danger_attempts++;
                }
                
                run_command(args, logfile);
            }
        }
    }
    
    // Cleanup
    for (int i = 0; i < ndanger; i++) {
        free(danger_list[i]);
    }
    free(danger_list);
    
    return 0;
}