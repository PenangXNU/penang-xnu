# Penang XNU

Pesquisa e implementação experimental de componentes para executar o kernel XNU público em plataformas ARM64 não Apple. O alvo físico inicial é o Motorola Moto G53 5G (`penang`, Qualcomm SM4350-AC/holi); o QEMU `virt` é usado nos testes de integração.

O objetivo futuro é executar um userland do iPadOS fornecido pelo próprio usuário, mantendo os binários originais. **Ainda não existe uma imagem de iPadOS utilizável ou um kernel XNU qualificado para boot no G53.** Nenhum firmware proprietário ou IPSW é distribuído neste repositório.

## Estado da pesquisa — 15/11/2026

| Frente | Evidência comprovada | Pendente |
| --- | --- | --- |
| QEMU/XNU | **QEMU_VERIFIED:** Q5 executou um dyld/main Mach-O experimental em EL0, realizou relocação e consumiu o ponteiro. | Não é o dyld da Apple, nem iPadOS completo; admissão de código ainda aberta. |
| BSD | **HOST_ONLY:** fixture ARM64 `getpid` compilado/linkado e testado. | Syscall real ainda não executada no XNU/QEMU. |
| Moto G53 | **DEVICE_VERIFIED:** Stage-0 próprio executado em RAM, EL1 e FDT observados. | **XNU não executado fisicamente**. |
| PMAP/Stage-2 | Contratos B4.2, guards e negativos testados no host. | Provider físico SCM/Stage-2/DMA e link nativo não qualificados. |

Veja [estado técnico atual](../research/RESUME-HERE.md), [resultado Q5](../research/2026-11-15-q5-el0.md) e [evidências do G53](../research/2026-11-15-physical-evidence-ledger.md).

A versão pública `v0.1.0` representa um **snapshot histórico** de 02/11/2026; seu [STATUS.md](../../STATUS.md) gerado não inclui os desenvolvimentos posteriores. Não confundir os dois estados.

## Validação e contribuições

```bash
python3 -B scripts/validate_public.py
```

Execute na raiz do repositório após instalar os requisitos do [README principal](../../README.md). A suíte pública valida contratos, código e testes host, não executa o telefone ou o XNU no QEMU.

Contribuições: [CONTRIBUTING.md](../../CONTRIBUTING.md), [AGENTS.md](../../AGENTS.md) e [GitHub Issues](https://github.com/PenangXNU/penang-xnu/issues). Segurança: [SECURITY.md](../../SECURITY.md). Licença do código próprio: Apache-2.0; modificações de XNU seguem a APSL-2.0.
