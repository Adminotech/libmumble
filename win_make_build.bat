@echo off
md install\include\libmumbleclient
md install\lib

xcopy /d /y src\*.h install\include\libmumbleclient
xcopy /d /y Mumble.pb.h install\include\libmumbleclient
xcopy /d /y src\Debug\*d.lib install\include\lib
xcopy /d /y src\Release\*.lib install\include\lib
