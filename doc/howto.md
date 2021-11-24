# "How-to guide" for readout

This page documents example situations and and how to handle them with the o2 Readout package.


## How to record files in small chunks ?

It's possible to configure the Readout fileRecorder consumer to split files in chunks of defined size. Actual file size may be smaller, rounded down to an entire number of superpages (a data page will never be split, a new chunk is created when the file size would exceed the size set).

Size of file is controlled by parameter _bytesMax_ (here: 1 GigaBytes), and maximum number of chunks to record by parameter _filesMax_ (here: 0, which means no limit). The string `%f` can be used in the _fileName_ parameter and substituted by the chunk counter at runtime (by default, appended at the end of the file).

```
[consumer-rec]
consumerType=fileRecorder
bytesMax=1G
filesMax=0
fileName=/tmp/data_%f.raw
```
