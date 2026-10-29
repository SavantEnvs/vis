// Leak detection is off by default for every ASan target in this fleet (SPEC §6.2 item 15).
extern "C" int __lsan_is_turned_off() { return 1; }
