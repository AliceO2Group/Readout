Here is a typical buffer organisation for a production FLP:
- there is one buffer per CRU end point or CRORC channel, providing empty data pages (superpages) to the hardware for DMA transfer from the readout card to the computer memory.
- there is one buffer for the FMQ channel to Data Distribution, to copy data which overlap data pages to a single block of memory. Data Distribution requires that data from a HeartBeatFrame (HBF) is shipped in a single FMQ message.

All buffers and data transfer operations are independant one from the other. If a single buffer gets full, the whole FLP system can be affected: CRU packets dropped, incomplete timeframes, data synchronization or consistency issues.

Buffers are circular, pages are used in the order they are put in the buffer. On startup, the pages are in the order of their memory address.
Buffer and pages size should be adapted to the throughput and data pattern at runtime.


In case of some "buffer low" issues, there are 3 log messages for each episode:

1) "buffer usage is high", when reaching 90% usage.
2) "buffer full" at 100% usage.
3) "buffer back to reasonable" when down to below 80% usage.

When one of the buffer is full, other messages will start to appear, depending on the context (no page left, packets dropped, etc).

