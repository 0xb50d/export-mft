# export-mft
Extracts master file table from volume(s)

# why?
Because my client's Windows Server 2008 refuses to run anything that require .NET or modern C runtime :(

# how to compile?
Can be compiled with mingw64 / mingw32 gcc
`gcc -static -o export-mft.exe export-mft.`

# how to use
Usage : `export-mft <volume letter> <output directory>`

Example : `export-mft c C:\temp`

Example : `export-mft all C:\temp`
