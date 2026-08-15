#!/usr/bin/env python3
# Prova que o CGI roda no diretorio do proprio script: abre "data.txt" por
# caminho relativo. Sem o chdir do CgiHandler este open() falharia.
#
# O arquivo e lido antes de qualquer saida porque os headers vem primeiro: em
# caso de falha o script emite "Status: 500" e o erro aparece como status code,
# em vez de virar um 200 com texto de erro dentro.
import os
import sys

try:
    data = open("data.txt").read()
except IOError as err:
    sys.stdout.write("Status: 500 Internal Server Error\r\n")
    sys.stdout.write("Content-Type: text/plain\r\n\r\n")
    sys.stdout.write("falhou ao abrir data.txt por caminho relativo: {}\n".format(err))
    sys.exit(0)

sys.stdout.write("Content-Type: text/plain\r\n\r\n")
sys.stdout.write("CWD: {}\n".format(os.getcwd()))
sys.stdout.write(data)
