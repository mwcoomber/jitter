## jitter

Tool for detecting OS jitter and running microbenchmarks.

Inspired by [Erik Rigtorp's hiccups](https://github.com/rigtorp/hiccups).

### Example

```
$ ./jitter -c 7 | column -t
cpu  min  mean       stddev    median  pct95  pct99.7  pct99.999  pct99.99999  max  samples    buffer
7    16   19.698741  1.044986  20      22     22       22         22           24   100663296  33554432
```
