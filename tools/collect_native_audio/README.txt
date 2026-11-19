FF7 Rebirth: extract four piano audio packages for troubleshooting

1. Extract this entire ZIP to a writable folder.
2. Double-click Collect.cmd on the PC with the Steam 1.004 installation.
   If it finds no game or several copies, paste the full path to the folder
   containing End. You can also drag that folder onto Collect.cmd to skip Steam
   discovery. Use the same copy that produced the log.
3. Send only the new FF7RPianoSongs-carriers-*.zip from the same folder.
   If there is an error, copy or screenshot the console instead.

For Windows 10/11 x64. No downloads, admin rights, or installs required.
The tool does not launch the game, change its files, or send anything over
the network. The output contains four raw game packages, not saves or logs.
The tool ZIP contains the pinned retoc Oodle decoder; the game folder does
not need a decoder DLL. The result ZIP does not include any DLLs.
The launcher relaxes PowerShell's script policy only for this invocation.

retoc source: https://github.com/trumank/retoc/tree/234cd0d86e5d2db21d187b8133efea456737802e
See retoc-LICENSE.txt for its license. The Microsoft VC runtime is app-local.
