pushd %~DP0
del /F  AIEditor.exe
del /F  AIEditor_ReadMe_txt_unused.bak
del /F  AIEditor_unused.bak
del /F  AIEditor_CHANGELOG.txt
del /F  AIEditor_¸Å´º¾ó.txt

del /F  ai_mmerged.txt
del /F  ai_merged.txt

del /F  npcpos_rush.txt
del /F  npcpos_kamaelrush.txt
del /F  npcpos_rush_old.txt

del /F eventdata.ini
del /F eventdata.xml

del /F commondropdata.xml

del /F itemdata_overseas.txt

del /F raidserverdata.xml
del /F OlympiadData.xml

del /F nasc.pdb
del /F nasc.bsc
del /F nasc64.pdb
del /F nasc64.bsc
del /F totalchecker.map
del /F totalchecker.pdb


rmdir /s /q htmleditor
rmdir /s /q sql
rmdir /s /q log
rmdir /s /q ai\obj
rmdir /s /q ai\sym

pause