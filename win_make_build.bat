@echo off

md install\libmumbleclient\include\mumbleclient
md install\libmumbleclient\lib

xcopy /d /y src\*.h install\libmumbleclient\include\mumbleclient
xcopy /d /y Mumble.pb.h install\libmumbleclient\include\mumbleclient
xcopy /d /y src\Debug\*d.lib install\libmumbleclient\lib
xcopy /d /y src\Release\*.lib install\libmumbleclient\lib
xcopy /d /y src\Debug\*d.dll install\libmumbleclient\lib
xcopy /d /y src\Release\*.dll install\libmumbleclient\lib

md install\celt\include\celt
md install\celt\lib

xcopy /d /y celt\libcelt\*.h install\celt\include\celt
xcopy /d /y celt-build\src\Debug\*d.lib install\celt\lib
xcopy /d /y celt-build\src\Release\*.lib install\celt\lib
