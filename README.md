# advanced-mini-shell
Advanced Mini Shell with Virtual Memory Simulation
A sophisticated shell implementation featuring virtual memory management, threading optimization, matrix calculations, and comprehensive process control. Built entirely in C with advanced system programming techniques.
Architecture Overview
This shell combines multiple complex systems:

Custom Virtual Memory Manager with TLB optimization and LRU page replacement
Multi-threaded Matrix Calculator with recursive memory reduction algorithms
Advanced Process Management with resource limiting and signal handling
Security Framework with dangerous command detection and partial matching
Custom I/O Redirection and pipe handling with comprehensive error management

Core Features
Virtual Memory Simulation (vmem)
A complete virtual memory management system that simulates hardware-level memory operations:

Page Table Management: Full page descriptor implementation with Valid, Dirty, and Permission bits
Translation Lookaside Buffer (TLB): Hardware-accurate TLB simulation with LRU replacement policy
Segmented Memory Model: Proper TEXT (read-only), DATA, BSS, and HEAP/STACK segment handling
Page Fault Handling: Automatic page loading from program files or swap with proper fault detection
LRU Page Replacement: Sophisticated eviction algorithm using global timestamp tracking
Swap File Management: Dynamic swap allocation with first-fit algorithm

The implementation handles memory addresses through complete virtual-to-physical translation, including permission checking for write operations to read-only segments.
bashvmem memory_script.txt
Multi-threaded Matrix Calculator (mcalc)
An advanced matrix computation system that demonstrates sophisticated threading and memory optimization:
Key Innovation: Recursive Memory Reduction
The threading system uses void pointers with upcasting to strings, combined with recursive algorithms to significantly reduce memory usage. Instead of using standard shared memory approaches, this implementation:

Recursively processes matrix pairs using custom void pointer management
Dynamically allocates and deallocates memory during recursive calls
Uses string upcasting to minimize memory footprint during operations
Implements divide-and-conquer threading where pairs are processed recursively until convergence

Threading Architecture:

pthread-based parallel matrix operations
Custom memory management that goes beyond assignment requirements
Recursive pair processing for multiple matrix operations
Dynamic memory allocation and cleanup in threaded environment

bashmcalc "(2,2:1,2,3,4)" "(2,2:5,6,7,8)" "ADD"
mcalc "(3,3:1,2,3,4,5,6,7,8,9)" "(3,3:9,8,7,6,5,4,3,2,1)" "SUB"
Advanced Process Control and Resource Management
Comprehensive process management with fine-grained resource control:

Resource Limiting (rlimit): CPU time, memory usage, file size, and file descriptor limits
Background Process Management: Full background execution with process tracking
Signal Handling: Complete signal-to-string conversion with detailed process termination reporting
Process Monitoring: Real-time statistics tracking with execution timing

bashrlimit set fsize=1MB mem=10MB cpu=30 command args
rlimit show
Custom I/O Operations
Enhanced I/O handling beyond standard shell capabilities:

Enhanced Tee Implementation: Custom my_tee with append functionality and multiple file support
Stderr Redirection: Proper 2> redirection handling with background process support
Pipe Operations: Single pipe implementation with multi-process coordination
Command Timing: Microsecond precision timing for all operations

Security and Command Validation
Robust security framework for command execution:

Dangerous Command Detection: Configurable blacklist with exact and partial matching
Command Validation: Input sanitization and argument count verification
Execution Statistics: Comprehensive tracking of executed, blocked, and dangerous commands
Performance Logging: Detailed execution logging with timing information

Technical Implementation Details
Memory Management Innovation
The virtual memory system implements a complete memory hierarchy:
Page Table Structure:
ctypedef struct {
    int V;           // Valid bit
    int D;           // Dirty bit  
    int P;           // Permission bit (1=read-only, 0=read-write)
    int frame_swap;  // Frame number or swap location
} page_descriptor;
TLB Implementation:

LRU replacement with timestamp tracking
Page-to-frame translation caching
Performance monitoring for hit/miss ratios
Automatic invalidation on page eviction

Threading Architecture with Memory Optimization
The matrix calculator uses an innovative threading approach that reduces memory usage through recursive processing:
Recursive Memory Reduction Algorithm:

Pair Processing: Takes n matrices and processes them in pairs using separate threads
Void Pointer Management: Uses void pointers that are upcast to strings for memory efficiency
Recursive Convergence: Continues recursively until single result matrix remains
Dynamic Allocation: Memory is allocated and freed within each recursive call to minimize footprint

This approach significantly reduces peak memory usage compared to traditional shared memory implementations, as it processes matrices in a divide-and-conquer fashion rather than loading all matrices simultaneously.
LRU Page Replacement Algorithm
Sophisticated page replacement using global time tracking:
cvoid update_page_access_time(int page_num) {
    page_access_time[page_num] = global_time_counter++;
}
The algorithm tracks access patterns across all memory segments and implements true LRU replacement based on actual access timestamps.
Advanced Signal Handling
Complete signal management with detailed reporting:

Signal-to-string conversion for all POSIX signals
Process termination analysis with exit code interpretation
Resource limit violation detection (SIGXFSZ, SIGXCPU)
Background process completion notification

Command Reference
Built-in Commands
Virtual Memory Operations:

vmem <script> - Execute virtual memory simulation from script file

Matrix Calculations:

mcalc <matrix1> <matrix2> <operation> - Perform threaded matrix operations
Supported operations: "ADD", "SUB"
Matrix format: "(rows,cols:data1,data2,...)"

Process Management:

rlimit show - Display current resource limits
rlimit set <resource>=<value> <command> - Set limits and execute command

I/O Operations:

my_tee [-a] <files> - Enhanced tee with append support

Advanced Shell Features
Background Execution:
Any command can be executed in background by appending &
Pipe Operations:
Single pipe support with full process coordination: command1 | command2
Error Redirection:
Stderr redirection to files: command 2> error.log
Resource Monitoring:
Real-time statistics displayed in shell prompt
Usage Examples
Virtual Memory Demonstration
bash# Create memory simulation script
echo "program.exe swap.swp 1024 512 256 1024 256 16 2048 4096" > vm_test.txt
echo "load 100" >> vm_test.txt
echo "store 200 A" >> vm_test.txt
echo "print table" >> vm_test.txt
echo "print ram" >> vm_test.txt

vmem vm_test.txt
Matrix Operations with Threading
bash# Multiple matrix addition using recursive threading
mcalc "(2,2:1,2,3,4)" "(2,2:5,6,7,8)" "(2,2:1,1,1,1)" "ADD"

# Large matrix operations demonstrating memory optimization
mcalc "(10,10:1,2,3...)" "(10,10:10,9,8...)" "SUB"
Resource Management
bash# Demonstrate file size limiting
rlimit set fsize=1MB dd if=/dev/zero of=testfile bs=2MB

# Memory and CPU limiting
rlimit set mem=50MB cpu=10 intensive_program
Build Instructions
bashgcc -o shell ex4.c -lpthread -D_GNU_SOURCE
./shell dangerous_commands.txt execution_log.txt
Requirements:
dangerous_commands.txt 
execution_log.txt
GCC with C99 support
POSIX-compliant system
pthread library
Linux/Unix environment

Performance Characteristics
Memory Efficiency:

Virtual memory system with on-demand paging
TLB caching reduces page table access overhead
Recursive threading minimizes peak memory usage in matrix operations

Timing Precision:

Microsecond-level timing using clock_gettime(CLOCK_MONOTONIC)
Statistical tracking of minimum, maximum, and average execution times
Performance logging for all successful command executions

Process Management:

Efficient background process handling with SIGCHLD
Resource limit enforcement at process level
Comprehensive error reporting and signal analysis

Shell Statistics and Monitoring
The shell prompt provides real-time performance metrics:
#cmd:15|#dangerous_cmd_blocked:2|last_cmd_time:0.00234|avg_time:0.00567|min_time:0.00001|max_time:0.02341>>
This includes:

Total commands executed
Number of dangerous commands blocked
Last command execution time
Average execution time across all commands
Minimum and maximum execution times recorded

Advanced Features
Error Handling:

Comprehensive input validation and sanitization
Graceful handling of system call failures
Memory allocation failure recovery
File operation error management

Security Implementation:

Configurable dangerous command detection
Partial string matching for command variants
Execution prevention with detailed logging
Statistics tracking for security events

System Programming Techniques:

Direct system call usage for performance
Custom memory management algorithms
Process synchronization and coordination
Signal handling and process control

Educational and Technical Value
This implementation demonstrates advanced understanding of:
Operating Systems Concepts:

Virtual memory management and address translation
Page replacement algorithms and memory hierarchy
Process management and inter-process communication
Resource allocation and system call interfaces

Systems Programming:

Low-level C programming with pointer manipulation
Multi-threading and synchronization primitives
Signal handling and process control
Memory optimization and performance tuning

Algorithm Design:

LRU implementation with efficient data structures
Recursive algorithms for memory optimization
String parsing and manipulation techniques
Error detection and recovery strategies

