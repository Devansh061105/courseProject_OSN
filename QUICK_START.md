# Docs++ - Quick Start Guide

## 📋 Overview

You have successfully set up the Docs++ distributed file system project in WSL. The project is located at `~/course-project`.

---

## 🎯 What You Need to Build

A Google Docs-like distributed file system with:
- **Name Server**: Central coordinator
- **Storage Servers**: File storage nodes (multiple)
- **Clients**: User interfaces (concurrent)

**Deadline**: November 18, 2025 (10 days from now)

---

## 📂 Current Project Structure

```
~/course-project/
├── src/
│   ├── common/          ✅ Basic utilities created
│   │   ├── protocol.h   # Message formats
│   │   ├── error_codes.h/.c  # Error definitions
│   │   ├── logger.h/.c  # Logging system
│   │   └── utils.h/.c   # Socket helpers
│   ├── name_server/     🔨 TODO: Implement
│   │   └── main.c
│   ├── storage_server/  🔨 TODO: Implement
│   │   └── main.c
│   └── client/          🔨 TODO: Implement
│       └── main.c
├── data/                # Storage server data
├── logs/                # Log files
├── tests/               # Test scripts
├── Makefile             ✅ Working
├── README.md
├── IMPLEMENTATION_PLAN.md  ⭐ DETAILED GUIDE
└── question.md          # Original requirements
```

---

## 🚀 Getting Started

### 1. Access the Project
```bash
cd ~/course-project
```

### 2. Verify Compilation
```bash
make clean
make
```

### 3. Review the Implementation Plan
```bash
cat IMPLEMENTATION_PLAN.md | less
```

---

## 📋 Implementation Checklist (Priority Order)

### Week 1 (Days 1-7): Core System

#### Phase 1: Infrastructure (Days 1-3)
- [x] Project structure setup
- [x] Common utilities (protocol, logger, sockets)
- [x] Makefile working
- [ ] Complete socket wrapper functions
- [ ] Add comprehensive logging
- [ ] Test basic client-server communication

#### Phase 2: Name Server (Days 4-6)
- [ ] Initialize NM server on port 8000
- [ ] Handle SS registration
- [ ] Handle client connections
- [ ] Implement Trie/HashMap for file search (< O(N))
- [ ] Implement LRU cache
- [ ] Build request routing logic
- [ ] Implement access control lists (ACL)

#### Phase 3: Storage Server (Days 7-9)
- [ ] Initialize SS, register with NM
- [ ] Implement file I/O operations
- [ ] Build sentence parser (delimiters: . ! ?)
- [ ] Implement sentence-level locking
- [ ] Handle file persistence (save/load)
- [ ] Implement undo mechanism (single level)

### Week 2 (Days 8-10): Features & Testing

#### Phase 4: Client (Days 8-9)
- [ ] Client initialization
- [ ] Command parser for all operations
- [ ] NM communication
- [ ] Direct SS communication
- [ ] Interactive WRITE mode

#### Phase 5: User Commands (Days 10-14)
**Basic (40 marks)**:
- [ ] VIEW (with flags -a, -l, -al)
- [ ] READ
- [ ] CREATE
- [ ] INFO

**Core (70 marks)**:
- [ ] WRITE (complex - allocate 2 days!)
- [ ] DELETE
- [ ] UNDO
- [ ] LIST
- [ ] STREAM (word-by-word, 0.1s delay)

**Advanced (40 marks)**:
- [ ] ADDACCESS/REMACCESS
- [ ] EXEC (execute shell commands)

#### Phase 6: System Requirements (Days 15-16)
- [ ] Data persistence
- [ ] Complete logging
- [ ] Error handling
- [ ] Access control enforcement

#### Phase 7: Testing (Days 17-18)
- [ ] Basic functionality tests
- [ ] Concurrent access tests
- [ ] Stress tests
- [ ] Memory leak checks (valgrind)

---

## 🔑 Key Implementation Details

### 1. WRITE Command (Most Complex - 30 marks!)

**Format**:
```
Client: WRITE filename sentence_index
Client: word_index content
Client: word_index content
...
Client: ETIRW
```

**Critical Points**:
- Lock only the specific sentence being edited
- Handle delimiter insertion (., !, ?) → creates NEW sentences
- "e.g. test" → ["e.", "g.", " test"] (every period splits!)
- Update sentence indices dynamically after splits
- Save undo backup before modification
- Release lock only after ETIRW

### 2. Sentence-Level Locking

```c
// Multiple clients can:
- Read same file simultaneously ✅
- Write different sentences simultaneously ✅
- Write same sentence simultaneously ❌ (one gets locked error)

// Use pthread_mutex for each sentence
```

### 3. Efficient Search (< O(N) - 15 marks)

Implement either:
- **Trie**: O(L) where L = filename length
- **HashMap**: O(1) average case
- **LRU Cache**: Cache recent searches (100-500 entries)

### 4. Direct SS Communication

For READ, WRITE, STREAM:
1. Client → NM: "I want to READ file.txt"
2. NM → Client: "Connect to SS at 192.168.1.10:9000"
3. Client → SS: Establish new TCP connection
4. SS → Client: Transfer data directly

### 5. Data Persistence

All files and metadata must survive SS restart:
```
data/ss1/
├── files/
│   └── test.txt           # Actual content
└── metadata/
    ├── test.meta          # JSON metadata
    └── backups/
        └── test_undo.txt  # Undo backup
```

---

## 💡 Critical Tips

### DO's ✅
1. **Start Simple**: Implement CREATE, READ, VIEW before WRITE
2. **Test Early**: Test each feature as you implement it
3. **Use Git**: Commit after every working feature
4. **Focus Core First**: Skip bonus if time is tight
5. **Debug with Logs**: Use the logging system extensively
6. **Handle Errors**: Clear error messages for all cases

### DON'Ts ❌
1. **Don't wait**: Start implementing TODAY
2. **Don't skip testing**: Bugs compound quickly
3. **Don't optimize early**: Get it working first
4. **Don't leave WRITE for last**: It's the hardest (30 marks!)
5. **Don't forget persistence**: Files must survive restarts
6. **Don't ignore warnings**: Fix compiler warnings

---

## 🧪 Testing Strategy

### Manual Testing
```bash
# Terminal 1: Name Server
cd ~/course-project
./name_server

# Terminal 2: Storage Server 1
./storage_server 1

# Terminal 3: Storage Server 2
./storage_server 2

# Terminal 4: Client
./client
> user1
> CREATE test.txt
> WRITE test.txt 0
> 1 Hello world.
> ETIRW
> READ test.txt
```

### Concurrent Testing
```bash
# Test two clients writing different sentences (should work)
# Test two clients writing same sentence (one should fail with LOCKED)
```

### Memory Leak Check
```bash
valgrind --leak-check=full ./name_server
```

---

## 📊 Grading Breakdown

| Feature | Marks | Priority |
|---------|-------|----------|
| VIEW | 10 | High |
| READ | 10 | High |
| CREATE | 10 | High |
| INFO | 10 | High |
| **WRITE** | **30** | **CRITICAL** |
| UNDO | 15 | High |
| STREAM | 15 | Medium |
| LIST | 10 | Low |
| DELETE | 10 | Medium |
| ACCESS | 15 | Medium |
| EXEC | 15 | Medium |
| **Subtotal** | **150** | |
| Persistence | 10 | High |
| Logging | 5 | Medium |
| Errors | 5 | Medium |
| Access Control | 5 | High |
| Efficient Search | 15 | High |
| **System Total** | **40** | |
| Specifications | 10 | Medium |
| **Base Total** | **200** | |
| Bonus Features | 50 | Optional |

---

## 🎯 Daily Goals (Suggested)

### Days 1-3 (Nov 8-10): Foundation
- Complete common utilities
- Test socket communication
- Basic logging working

### Days 4-6 (Nov 11-13): Name Server
- NM initialization
- SS registration
- Client handling
- File mapping with Trie

### Days 7-9 (Nov 14-16): Storage Server
- SS initialization
- File operations
- Sentence parsing & locking
- **Start WRITE implementation**

### Days 10-11 (Nov 17-18): Client & Commands
- Client implementation
- Command parsing
- Basic commands (VIEW, READ, CREATE)

### Days 12-14 (Nov 19-21): Core Features
- **Complete WRITE** (allocate 2 days!)
- DELETE, UNDO, LIST
- STREAM, ACCESS, EXEC

### Days 15-16 (Nov 22-23): System Requirements
- Persistence
- Logging polish
- Error handling

### Days 17-18 (Nov 24-25): Testing & Polish
- Integration tests
- Bug fixes
- Documentation

### Days 19-20 (Nov 26-27): Buffer & Submission
- Final testing
- Optional bonus features
- Code cleanup

---

## 🔧 Useful Commands

### Build & Run
```bash
make clean && make              # Rebuild everything
./name_server                   # Start name server
./storage_server 1              # Start SS with ID 1
./client                        # Start client
```

### Development
```bash
# Watch logs in real-time
tail -f logs/name_server.log
tail -f logs/ss1.log

# Check for memory leaks
valgrind --leak-check=full ./name_server

# Debug with gdb
gdb ./name_server
```

### Git (Recommended)
```bash
git init
git add .
git commit -m "Initial project setup"
git log --oneline              # View commit history
```

---

## 📚 Key Resources

1. **IMPLEMENTATION_PLAN.md**: Detailed implementation guide (37KB!)
2. **question.md**: Original requirements
3. **Beej's Guide**: TCP socket programming
4. **POSIX Threads**: For concurrency
5. **Trie Data Structure**: For efficient search

---

## ⚠️ Common Pitfalls

1. **Delimiter Handling**: "e.g." creates 2 sentences, not 1
2. **Deadlocks**: Always lock sentences in ascending order
3. **Memory Leaks**: Use valgrind, free all malloc'd memory
4. **Buffer Overflows**: Use strncpy, validate lengths
5. **Network Timeouts**: Set socket timeouts
6. **Race Conditions**: Protect shared data with mutexes

---

## 🏁 Success Criteria

- [ ] All 11 commands working
- [ ] Concurrent access with locking
- [ ] Data persists across restarts
- [ ] Search is O(L) or O(1), not O(N)
- [ ] No memory leaks (valgrind clean)
- [ ] Comprehensive logging
- [ ] Clear error messages
- [ ] Handles 100+ concurrent clients
- [ ] Compiles with no errors

---

## 🎓 Final Advice

**This is a BIG project**. You have 10 days. Here's the reality check:

- **Days 1-3**: If foundation isn't done, you're behind
- **Days 4-9**: If Name Server & Storage Server aren't working, catch up fast
- **Days 10-14**: If WRITE isn't working by day 12, you're in trouble
- **Days 15-18**: Testing is NOT optional
- **Days 19-20**: Buffer for emergencies only

**Work distribution** (for a team of 2-3):
- Person 1: Name Server + Testing
- Person 2: Storage Server (focus on WRITE)
- Person 3: Client + Documentation

**Meet daily**, sync code, review each other's work.

---

## 🚨 Need Help?

1. Check IMPLEMENTATION_PLAN.md (detailed examples)
2. Review question.md (requirements)
3. Use the doubts document (mentioned in question.md)
4. Ask TAs early, don't wait!

---

## ✅ Next Steps RIGHT NOW

1. **Read IMPLEMENTATION_PLAN.md thoroughly**
2. **Understand the WRITE command** (most complex)
3. **Start coding Phase 1** (complete utilities)
4. **Test socket communication** between components
5. **Set up Git** for version control

---

**Remember**: You CAN do this. Break it down, work incrementally, test continuously.

**Good luck! 🚀**

---

## 📞 Quick Links

- Project location: `~/course-project`
- Detailed guide: `IMPLEMENTATION_PLAN.md`
- Requirements: `question.md`
- Build: `make`
- Clean: `make clean`
- Test: `make test` (once you create tests)

**Deadline**: November 18, 2025, 11:59 PM IST
