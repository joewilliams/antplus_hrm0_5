### hrm0_5

This code was originally found at https://forums.garmin.com/archive/index.php/t-1281.html

Specifically at http://www.sbrk.co.uk/hrm0_5.tar.gz

I have made a few modifications to use it as a port in Erlang.

The branch "original" has the original code in it for posterity.

It seems to work with Garmin ANT USB stick, I haven't been able to get it to work with the SparkFun stick.

#### usage

`hrm -g -t 5 -f /dev/ttyUSB0`
