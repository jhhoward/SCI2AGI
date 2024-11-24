# SCI2AGI
This is a collection of tools to convert resources from Sierra's SCI engine to AGI engine

## PIC2PIC
Converts an AGI background PICTURE resource to a SCI PICTURE resource. 
Available command line arguments:
```
-o [path] To specify output path
-d To dump files to PNG
-v verbose mode
-y [value] offset y output
```

NOTE: The way that priority bands are configured in SCI and AGI differ which can lead to some problems with how object appear in relation to background elements (e.g. sprites appearing in front of background elements that they should be behind). The best solution I could come up with is to use the AGI command `set.pri.base(63);` which reconfigures the priority bands to be closer in spacing and positioning to SCI's default setup. You will need to use an AGI interpreter of version 2.936 or above to use this.

PIC2PIC will make a best effort to avoid flood fill issues but some can still occur due to the change in resolution between AGI and SCI backgrounds. One other limitation is that the pattern brush commands are not fully implemented so some pictures may contain missing details.

## VIEW2VIEW
Converts an AGI sprite VIEW resource to a SCI VIEW resource.
```
-o [path] To specify output path
-d To dump files to PNG
-v verbose mode
```

## SND2SND
Converts an AGI SOUND resource to a SCI SOUND resource.
```
-o [path] To specify output path
-v verbose mode
-c manually map channels
```
By default the converter will attempt to map the PC speaker track to voice 0 and populate voice 1 and 2 with the Tandy tracks. You can manually assign channel mapping too. For example:
`SND2SND sound.001 -c=5,1,4,2`
Will map voice 0 to SCI track 5, voice 1 to SCI track 1, voice 2 to SCI track 4 and the noise voice to track 2.
