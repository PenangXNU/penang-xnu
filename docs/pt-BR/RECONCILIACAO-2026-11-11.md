# Reconciliação de pesquisa — 11/11/2026

Esta branch recupera o código original do AIR2IR3 (41 arquivos, hashes Git conferidos), seus testes sintéticos e a documentação de checkpoints posteriores à versão pública v0.1.0.

A P0.16 chegou à montagem real de RAMDisk/ROMFS e à tentativa de executar /sbin/launchd no QEMU. A sonda M11-D3A1 executou Stage-0 no Moto G53 físico, mas **não** executou XNU. M12-T0/T2/T3 avançaram a ponte, planejamento de memória e testes Stage-1 no QEMU. PMAP físico, identidade integral da FDT no handoff, link nativo, entropia física e boot XNU continuam bloqueados ou não executados.

Os binários e dumps Apple/Qualcomm/Motorola, dados privados e logs brutos não são publicados. A documentação principal em inglês está em [índice de pesquisa](../research/README.md). A branch não é uma nova release e ainda precisa de revisão independente antes de integração à main.
