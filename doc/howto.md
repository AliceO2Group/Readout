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


## How to launch custom commands on start/stop

First, custom command shells must be enabled in the default readout settings `/etc/o2.d/readout/readout-defaults.cfg`:
```
[readout]
...
customCommandsEnabled=1
```
This launches a sub-process shell on readout startup to execute commands later.

Second, custom commands should be defined in the readout configuration file for associated state transitions: pre|post + START|STOP.
For example, to start/stop internal ROC CTP emulator automatically with readout:
```
[readout]
...
customCommands=postSTART=roc-ctp-emulator --id=#1 --trigger-mode periodic --trigger-freq 40000 --hbmax 255 --bcmax 3563,preSTOP=o2-roc-ctp-emulator --id=#1 --eox
```
