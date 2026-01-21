### build in edk2
```bash
build -p OCConfigSelector/OCConfigSelector.dsc -a X64 -t GCC5 -b RELEASE
```
### using
put config-xx.plist in /EFI/OC/
```plist
<key>Tools</key>
<array>
  <dict>
    <key>Arguments</key>
    <string></string>
    <key>Auxiliary</key>
    <true/>
    <key>Comment</key>
    <string></string>
    <key>Enabled</key>
    <true/>
    <key>Flavour</key>
    <string>Auto</string>
    <key>FullNvramAccess</key>
    <true/>
    <key>Name</key>
    <string>OCConfigSelector</string>
    <key>Path</key>
    <string>OCConfigSelector.efi</string>
    <key>RealPath</key>
    <true/>
    <key>TextMode</key>
    <true/>
  </dict>
</array>
```
