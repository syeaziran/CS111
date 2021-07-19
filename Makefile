#NAME: Xuan Peng
#ID:705185066
#EMAIL:syeaziran@g.ucla.edu

.SILENT:

.PHONY: default clean dist
SUBFILES = lab4c_tcp.c lab4c_tls.c Makefile README

default:
	gcc -Wall -Wextra -g -lm -lmraa lab4c_tcp.c -o lab4c_tcp
	gcc -Wall -Wextra -g -lm -lmraa -lssl -lcrypto lab4c_tls.c -o lab4c_tls

clean:
	rm -rf lab4c_tcp lab4c_tls lab4c-705185066.tar.gz *txt

dist:
	tar -czvf lab4c-705185066.tar.gz $(SUBFILES)
