@echo off
md install\include\mumbleclient
md install\lib

xcopy /d /y src\*.h install\include\mumbleclient
xcopy /d /y Mumble.pb.h install\include\mumbleclient
xcopy /d /y src\Debug\*d.lib install\lib
xcopy /d /y src\Release\*.lib install\lib
