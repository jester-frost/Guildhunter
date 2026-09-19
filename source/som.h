/* Toca os sons do Guild Hunter. A sintese esta no nucleo (testavel no PC);
   aqui fica so o que depende do 3DS: o DSP e o cartao. */
#ifndef GUILD_HUNTER_SOM_H
#define GUILD_HUNTER_SOM_H

/* `sem_musica` abre sem a trilha de fundo. Serve para isolar: o streaming é
   a parte mais complicada do app (lê do cartão o tempo todo, em blocos que se
   revezam), e quando algo trava é o primeiro suspeito. Sem ela, os efeitos
   continuam funcionando. */
void som_iniciar(int sem_musica);
void som_encerrar(void);
/* Chamar ao voltar do menu HOME ou do sono: o sistema toma o DSP enquanto o
   app está suspenso e não o devolve pronto. Sem isto, o app volta mudo. */
void som_retomar(void);
void som_tocar(int id);
/* 0 quando o DSP nao subiu -- o app segue funcionando, calado. */
int  som_disponivel(void);
/* Quantos sons vieram de arquivo do cartao em vez da sintese. */
int  som_substituidos(void);

/* Musica de fundo: sdmc:/3ds/guild-hunter/sons/musica.wav, em loop.
   Nao vem no repositorio -- e o arquivo de quem instalou. */
int  musica_existe(void);
int  musica_tocando(void);
void musica_alternar(void);
/* Chamar a cada quadro: reabastece o streaming. */
void musica_passo(void);

#endif
