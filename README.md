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
