# Sistema de Votação Distribuído (Trabalho - Computação Distribuída)

## Compilação
Requer gcc e pthreads.
```
make
```

## Execução
1. Inicie o servidor (porta e pelo menos 3 opções):
```
./server 9090 Alice Bob Carol
```

2. Inicie clientes (pelo menos 20 podem ser abertos):
```
./client 127.0.0.1 9090
```

3. No cliente, envie os comandos (linha a linha):
```
HELLO <VOTER_ID>
LIST
VOTE <OPCAO>
SCORE
BYE
```
Exemplo:
```
HELLO user01
LIST
VOTE Alice
SCORE
BYE
```

### Comando administrativo
Para fechar a eleição e forçar a geração de `resultado_final.txt`, conecte com `VOTER_ID` igual a `ADMIN`:
```
HELLO ADMIN
ADMIN CLOSE
```
Após `ADMIN CLOSE`, novas tentativas de `VOTE` receberão `ERR CLOSED`. `SCORE` passará a retornar `CLOSED FINAL ...`.

## Logs e arquivos
- `eleicao.log` registra eventos do servidor.
- `resultado_final.txt` contém o placar final quando a eleição é encerrada (criado por ADMIN).

## Observações / Limitações
- Autenticação é simples: qualquer cliente que faça `HELLO ADMIN` é considerado administrador.
- O servidor guarda eleitores em memória (array estático). Para muitos eleitores pode ser preciso ajustar `MAX_VOTERS`.
- Cliente possui interface minimalista para testes manuais. Para testes automatizados, envie comandos terminados em newline através de netcat ou scripts.

## Testes sugeridos
- Voto único: tente votar duas vezes com o mesmo `VOTER_ID`.
- Opção inválida: `VOTE Ziggy` → `ERR INVALID_OPTION`
- 20 clientes simultâneos: abra 20 instâncias do cliente.
- Cliente que cai antes de votar: encerre cliente sem enviar `VOTE` → voto não contado.
- Admin encerra votação: `ADMIN CLOSE` e depois `VOTE` recebe `ERR CLOSED`.

