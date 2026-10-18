@echo off
echo init directories for l2encoder
	rmdir /s /q release
	rmdir /s /q input
	rmdir /s /q output
	rmdir /s /q temp
	mkdir release
	mkdir input
	mkdir output
	mkdir temp

echo copy all files in current directory to release directory
	copy *.* .\release

echo copy l2server.ini to release directory
	copy ..\l2server\l2server.ini .\release

echo delete needless files in release directory
	del /q .\release\l2encoding.bat
	del /q .\release\l2encoder.exe

echo move all text files in release directory to input directory
	move .\release\*.txt .\input

echo move eventdata.ini in release directory to input directory
	move .\release\eventdata.ini .\input

echo move l2server.ini in release directory to input directory
	move .\release\l2server.ini .\input

echo Don't encode some txt files. Move that files to release directory
	move .\input\*pch*.txt 		.\release
	move .\input\ai_script.txt 	.\release
	move .\input\ai_macro.txt 	.\release
	move .\input\ai_merged.txt 	.\release
	move .\input\ai_mmerged.txt 	.\release
	move .\input\freewayinfo.txt 	.\release
	move .\input\fstring.txt 		.\release
	move .\input\maker_script.txt 	.\release
	move .\input\npcpos.txt 		.\release
	move .\input\npcpos_2bae.txt	.\release
	move .\input\npcpos_rush.txt	.\release
	move .\input\npcpos_rush_old.txt	.\release
	move .\input\PCCafeCoupon.txt 	.\release
	move .\input\petitioncategory.txt .\release
	move .\input\superpointinfo.txt 	.\release
	move .\input\watch_list.txt 	.\release
	move .\input\AIEditor_¸Å´º¾ó.txt	.\release
	move .\input\q1.txt		.\release
	move .\input\q2.txt		.\release
	move .\input\compile_msg.txt	.\release
	move .\input\AIEditor_CHANGELOG.txt	.\release
	move .\input\PCCafeCoupon_Bulk.txt	.\release

echo start encoding.
	.\l2encoder.exe

echo complete to encode

echo copy encoded files in output directory to release directory
	copy .\output\*.* .\release

echo prepare release directories and files
	move .\release\*.* .\temp
	mkdir .\release\script
	mkdir .\release\CachedScript
	mkdir .\release\l2server
	move .\temp\l2server.ini.l2enc 		.\release\l2server
	move .\temp\*.* 				.\release\script
	copy .\release\script\itemdata.txt.l2enc .\release\cachedscript

echo end of script encoding.
pause
echo on