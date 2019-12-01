# Modtimer
Kernel module that generates random numbers and insert them in a linked list.

The module allows a single program to "consume" the list numbers by reading from the entry ```/proc/modtimer```. The program will crash if the list is empty when reading.
The number generation process (managed by a timer) is active while a user program is reading from the `/proc` input.

The module consists of three layers:
* Top Half: Kernel timer that generates sequence of numbers and inserts them into a circular buffer delimited
* Bottom Half: Deferred task that transfers the integers from the circular buffer to the linked list.
* Upper Layer: Implementation of operations associated to the entries `/proc` exported by the module.

![image explaining layers](https://imgur.com/akVs90l.png)


![diagram](https://imgur.com/heKFbqr.png)


## Execution example

### Terminal 1

```bash
  kernel@debian:~$ sudo insmod modtimer.ko
  kernel@debian:~$ cat /proc/modconfig
  timer_period_ms=500
  emergency_threshold=80
  max_random=300
  kernel@debian:~$ cat /proc/modtimer
  61
  176
  74
  298
  87
  221
  100
  235
  114
  249
  128
  7
  141
  20
  ^C
```

### Terminal 2
```bash
  kernel@debian:~$ sudo tail -f /var/log/kern.log
  ....
  Jan 3 18:13:30 kernel kernel: [161532.116030] Generated number: 61
  Jan 3 18:13:30 kernel kernel: [161532.652020] Generated number: 176
  Jan 3 18:13:31 kernel kernel: [161533.192020] Generated number: 74
  Jan 3 18:13:31 kernel kernel: [161533.728018] Generated number: 298
  Jan 3 18:13:32 kernel kernel: [161534.268018] Generated number: 87
  Jan 3 18:13:32 kernel kernel: [161534.804023] Generated number: 221
  Jan 3 18:13:33 kernel kernel: [161535.344022] Generated number: 100
  Jan 3 18:13:33 kernel kernel: [161535.884017] Generated number: 235
  Jan 3 18:13:33 kernel kernel: [161535.924681] 8 elements moved from the buffer to the list
  Jan 3 18:13:34 kernel kernel: [161536.424019] Generated number: 114
  Jan 3 18:13:34 kernel kernel: [161536.964019] Generated number: 249
  Jan 3 18:13:35 kernel kernel: [161537.504026] Generated number: 128
  Jan 3 18:13:36
  ...
```
