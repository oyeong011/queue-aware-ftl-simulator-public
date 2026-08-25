# Trace CSV schema

```
arrival_ns,op,lba,size_bytes,stream_id
0,WRITE,0,4096,0
1000,READ,8,4096,0
```

- `arrival_ns`: request arrival time in nanoseconds, monotonically non-decreasing.
- `op`: `READ` or `WRITE`.
- `lba`: logical page number (page-granular, not byte-addressed).
- `size_bytes`: must be a multiple of the simulator's `page_size_bytes`.
- `stream_id`: reserved for hot/cold stream tagging; parsed but not yet
  consumed by the FTL core (see limitations.md).
