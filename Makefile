.PHONY:d
d:
	$(error specify target)

.PHONY:upload
upload:
	rm -rf .upload
	mkdir -p .upload/zip/src

	cp dispswitch.sln .upload/zip/src/
	cp dispswitch.vcproj .upload/zip/src/
	cp main.cpp .upload/zip/src/

	cp dispswitch.exe .upload/zip/
	cp dispswitch.ini .upload/zip/
	cp dispswitch.html .upload/zip/

	cd .upload/zip/ && zip -9Rm ../dispswitch.zip *

	cp dispswitch.html .upload/index.html
	c:/bin/ncftp/ncftpput -u tomseddon ftp.plus.net htdocs/dispswitch .upload/index.html .upload/dispswitch.zip
