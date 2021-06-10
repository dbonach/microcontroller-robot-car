/* Projeto Final EA871 - FEEC - 2021
   Alunos:
   Deivit Lopes Bonach - RA:166508
   Jitesh Ashok Manilal Vassaram - RA:175867 */

/* Macro de clock do microcontrolador */
#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>

/* Funcao auxilar para alterar o funcinamento dos motores. */
void comandoMotores(void);

/* Funcao auxiliar para realizar a conversao da distancia
   de micro metro para centrimetos em ascii. */
void conversao(void);

/* Funcao auxiliar para calcular a distancia em micro metros. */
void calculoDistancia(void);

/* Mensagens que serao exibidas. */
char msg_w[] = "FRENTE\n";
char msg_s[] = "TRAS\n";
char msg_a[] = "ANTI-HORARIO\n";
char msg_d[] = "HORARIO\n";
char msg_q[] = "PARADO\n";
char msg_e[] = "DDDcm\n"; 
char msg_6[] = "Velocidade 60%\n";
char msg_8[] = "Velocidade 80%\n";
char msg_0[] = "Velocidade 100%\n";
char msg_w2[] = "OBSTACULO!\n";

/* Ponteiros que irao apontar para o primeiro caractere da
   mensagem respectiva e que sera utilizada na transmissao. */
char * mensagem_w = &(msg_w[0]);
char * mensagem_s = &(msg_s[0]);
char * mensagem_a = &(msg_a[0]);
char * mensagem_d = &(msg_d[0]);
char * mensagem_q = &(msg_q[0]);
char * mensagem_e = &(msg_e[0]);
char * mensagem_6 = &(msg_6[0]);
char * mensagem_8 = &(msg_8[0]);
char * mensagem_0 = &(msg_0[0]);
char * mensagem_w2 = &(msg_w2[0]);

/* Variavel principal que recebe o comando externo. */
volatile char caractere = 'q';
/* Variavel auxiliar para salvar o comando anterior. */
volatile char charAntigo = 'q';

/* Variaveis que irao auxiliar na conversao do valor da distancia para codigo ascii. */
unsigned int a;
unsigned int b;
unsigned int c;
unsigned long int valor = 0;

/* Sinalizador de obstaculo. */
volatile unsigned char obstaculo = 1;

/* Variavel auxiliar para contabilizar a quantidade de
   interrupcoes associadas ao vetor TIMER0_OVF_vect. */
volatile unsigned int contadorTimer0 = 0;

/* Variavel que armazena a distancia do sensor a algum objeto. */
volatile unsigned long int distancia = 9990000;

volatile int calculo = 0; // flag que controla se o calculo da distancia foi finalizada ou nao

/* Variavel auxiliar para contalibiza a quantidade de ciclos de
   interrupcao entre o inicio e o final do echo.  */
volatile unsigned int ciclosEcho = 1;

/* Variavel auxiliar para contabilizar o numero
   da interrupcao no inicio do echo. */
volatile unsigned int inicioEcho = 0;

/* Variavel auxiliar que eh incrementada a cada 200ms. */
volatile unsigned int contador1segundo = 0;

/* Flags auxiliares para garantir o envio completo das mensagens
   do comando 'w' e obstaculo, sem que uma "atropele" a outra. */
volatile unsigned char MensagemObstaculo = 0;
volatile unsigned char MensagemFrente = 0;

/* Flag para intercalar os comandos dentro do vertor
   de interrupcao Timer1 que implementa o funcionamento
   do led que indica a distancia. */
volatile unsigned char flag = 1;

/* Fator de multiplicacao para gerar um valor a ser carregado no
   registrador OCR1A que define o top de contagem do TCNT1/Contador1,
   assim definindo a frequencial em que o led pisca. 
  
   O valor escolhido resulta em:
- Distancia maxima de 336cm eh para gerar 391 interrupcoes, intervalo do led de 2s. 
- Distancia de 10cm eh para gerar 11 interrupcoes, intervalo do led de 0,06s. */
unsigned int multiplicador = 80;
 

/* Configuracao do sinal PWM gerado. */
void setupMotor(void)
{
    /* Define um valor intermediario de contagem entre 0 e 255 a partir
       do qual o sinal de saida no pino PD03/OC2B passa de nivel alto para
       nivel baixo no modo (non-inverting mode),
       ou seja, define o duty cicly do sinal PWM.
       Deste modo para OCR2B = 153 inicialmente o duty cicle eh de 60%. */
    OCR2B = 153;

    /* TCCR2B - Timer/Counter Control Register B
       FOC2A FOC2B  -  -  WGN22 CS22 CS21 CS20
         0     0    -  -    0     0    1    1
    
       Para prescaler = 32, os bits CS21 e CS20 precisam ser setados.
       Fazendo WGM22 = 0, WGM21 = 1 E WGM20 = 1, temos o modo fast PWM com
       valor maximo de contagem 0xFF (255). */
    TCCR2B = 0x03;
    
    /* TCCR2A - Timer/Counter Control Register A
       COM2A1 COM2A0 COM2B1 COM2B0  -  -  WGM21 WGM20
         0      0      1      0     -  -    1     1 

       Fazendo COM2B1 = 1 e COM2B0 = 0, temos o modo "nao invertido" do sinal
       PWM no pino PD03/OC2B, onde a saida eh setada quando TCNT2 vai a zero
       e zerada quando TCNT2 se iguala a OCR2B. */
    TCCR2A = 0x23;

    /* Configura PD03/OC2B como saida do sinal PWM para os motores. */
    DDRD |= 0x08;

    /* A1 (PC1), A2 (PC2), A3 (PC3), A4 (PC4) como saida*/
    DDRC |= 0x1E;

    /* Inicialmente os motores estao parados,
       logo temos: A1 = A2 = A3 = A4 = 0 */
    PORTC &= ~0x1E;
}

/* Configuracao do modo de operacao da USART */
void setupUsart(void) 
{
    /* UBRR0H e UBRR0L: baud rate = 9600 bps - ubbr0 = 103 (decimal) */
    UBRR0H = 0x00;
    UBRR0L = 0x67;
    
    /* UCSR0A: modo de transmissao normal - bit U2X0 = 0 
       modo multiprocessador desabilitado - bit MPCM0 = 0 
       Como esta sendo utilizado apenas os bits 0 e 1 do registrador UCSR0A, entao
       apenas esses dois bits sao alterados para 00, com o intuito de garantir que o multiprocessamento
       e o double-speed estejam desabilitados. Quanto aos outros 6 bits, como o valor deles nao importa 
       no programa, entao sao mantidos com nivel logico = 1. */
    UCSR0A &= ~(0x03);
    
     /* UCSR0B: habilita a interrupcao do tipo "recepcao completa" - bit RXCIE0 = 1 
               desabilita a interrupcao do tipo "transmissao completa" - bit TXCIE0 = 0
               desabilita a interrupcao do tipo "registrador de dados vazio" - bit UDRIE0 = 0 
               habilita o receptor - bit RXEN0 = 1
               habilita o transmissor - bit TXEN0 = 1 
               numero de bits transmitidos por frame = 8 - bit UCSZ02 = 0 
        Como nao esta sendo explorado o 9 bit de dado de um frame nesse programa, entao
        os bits referentes a essa configuracao - 0 e 1 do registrador UCSR0B - nao serao 
        alterados, logo apenas os bits de 2 a 7 sao manipulados. */
    UCSR0B &= 0x9B;
    UCSR0B |= 0x98;
    
    /* UCSR0C: modo de operacao assincrono - bit UMCEL01 e UMCEL00 = 00 
               sem bits de paridade - bit UPM01 e UPM00 = 00
               numero de stop bit = 1 - bit USBS0 = 0 
               polaridade do clock modo assincrono - bit UCPOL0 = 0
               numero de bits transmitidos por frame = 8 - bit UCSZ01 e UCSZ00 = 11 */
    UCSR0C = 0x06;
}


/* Configuracao do Timer 1 para gerar interrupcoes
   e controlar o envio de mensagens pela USART. */
void setupLed(void)
{
    /* PC5/A5 como saida. */
    DDRC |= 0x20;
      
    /* PC5/A5 inicialmente apagado. */
    PORTC &= ~0x20;

    /* OCR1A sera atualizado durante o funcionamento, definindo
       a frequencia em que o led pisca. O periodo eh dado por:
       1024*(391 * multiplicador + 1)/16e6 */
    OCR1A = 391 * multiplicador;

    /* Seta OCIE1A (bit 1) para gerar uma interrupcao toda vez que o registrador
       TCNT1 zerar apos ocorrer um match entre ele e o registrador OCR1A. */
    TIMSK1 = 0x02;

    /* TCCR1B - Timer/Counter Control Register B
       FOC1A FOC1B  -  -  WGM12 CS12 CS11 CS10
         0     0    -  -    1     1    0    1
    
       Para prescaler = 1024, os bits CS12 e CS10 precisam ser setados.
       Para o modo de operacao 4 - CTC com top de contagem em OCR1A
       apenas o bit WGM12 precisa ser setado.  */
    TCCR1B = 0x0D;


    /* TCCR1A - Timer/Counter Control Register A
       COM1A1 COM1A0 COM1B1 COM1B0  -  -  WGM11 WGM10
         0      0      0      0     -  -    0     0 

       Para modo de operacao normal das portas OC1A e OC1B, os
       bits COM1A1, COM1A0, COM1B1 E COM1B0 precisam ser zero. */
    TCCR1A = 0x00;
}

/* Rotina de configuracao para utilizar o sensor ultra sonico.
   Configuracao do Timer/Counter0. */
void setupSensor(void)
{
    /* Terminal A0 (PC0) como saida para gerar um pulso
       de 50us destinado ao sensor ultra sonico. */
    DDRC |= 0x01;

    /* Inicialmente PC0 esta em nivel baixo. */
    PORTC &= ~(0x01);

    /* Terminal PD2 (INT0) como entrada para receber o echo enviado pelo sensor. */
    DDRD &= ~(0x04);

    /* Para os parametros utilizados uma nova interrupcao eh gerada a cada
       1*8*(99 + 1)/16e6 = 50us.
       O Timer0 foi configurado para gerar a base de tempo necessaria para medir
       os seguintes parametros:
       - O intervalo de tempo de 200ms (4000 interrupcoes) para gerar um novo pulso
       de 50us em PC0.
       - Gera o pulso de 50us (1 interrupcao) em PC0 destinado ao sensor ultra sonico.
       - Temporizar o echo proveniente do sensor ultra sonico atraves do registro
       da numero da interrupcao em que houve uma borda de subida em INT0 e depois
       o numero da interrupcao em que houve uma borda de descida, assim possibilitando
       o calculo da distancia do sensor ao objeto atraves da quantidade de interrupcoes.
       - O intervalo de tempo de 1s (4000 * 5 interrupcoe) para liberar o envio de uma nova mensagem. */

    /* Valor top (0d99) da contagem de TCNT0 para ter o periodo de 50us entre as interrupcoes. */
    OCR0A = 0x63;

    /* Seta OCIE0A (bit 1) para gerar uma interrupcao logo apos os valores de OCR0A
       e TCNT0 se igualarem, ou seja, toda vez em que a contagem de 0 a 99
       exceder o valor 99 e ir para 0. */
    TIMSK0 = 2;

    /* TCCR0B - Timer/Counter Control Register B
       FOC0A FOC0B  -  -  WGN02 CS02 CS01 CS00
         0     0    -  -    0     0    1    0
    
       Para preescaler = 8 apenas o bit CS01 precisa ser setado.  */
    TCCR0B = 0x02; // preescaler 8

    /* TCCR1A - Timer/Counter Control Register A
       COM0A1 COM0A0 COM0B1 COM0B0  -  -  WGM01 WGM00
         0      0      0      0     -  -    1     0 

       Para modo de operacao 2 - CTC, com TOP de contagem em
       OCR0A, apenas o bit WGM01 precisa ser setado.

       Para modo de operacao normal das portas OC0A e OC0B, os
       bits COM0A1, COM0A0, COM0B1 E COM0B0 precisam ser zero. */
    TCCR0A = 0x02;  // modo ctc, top em ocra

    /* Configuracao de interrupcao a partir de INT0. */

    /* Habilita a interrupcao a partir do pino INT0 (PD2) */
    EIMSK = 0x01;

    /* EICRA - External Interrupt Control Register A
         -   -   -   -  ISC11 ISC10 ISC01 ISC00
         -   -   -   -    0     0     0     1
       
       Setando o bit ISC00 qualquer alteracao do nivel
       logico em INT0 (PD2) gera uma interrupcao. */
    EICRA = 0x01;
}

ISR(USART_UDRE_vect) 
{
    /* dependendo do endereco que foi recebido do registrador de dados udr0, vai ser exibida 
       uma mensagem e alguma acao do motor sera executada a depender do caractere */
    if (caractere == 'w') {
        /* se a flag 'obstaculo' estiver setada, entao a distancia entre o carrinho e o objeto 
           a frente eh menor ou igual a 10cm, e a mensagem deve mudar para 'OBSTACULO' e ignorar o comando 'w' 
           assim como sua mensagem */
        if ((obstaculo && !MensagemFrente) || MensagemObstaculo) {
            MensagemObstaculo = 1;
            if (*mensagem_w2 != '\0') {
                UDR0 = *mensagem_w2;
                mensagem_w2++;
            }
            else {
                mensagem_w2 = &(msg_w2[0]);
                UCSR0B &= ~(0x20);
                charAntigo = caractere;
                MensagemObstaculo = 0;
            }
        }
        /* caso contrario, a mensagem do comando 'w' deve funcionar normalmente */
        else {
            MensagemFrente = 1;

            if (*mensagem_w != '\0') {
                UDR0 = *mensagem_w;
                mensagem_w++;
            }
            else {
                mensagem_w = &(msg_w[0]);
                UCSR0B &= ~(0x20);
                charAntigo = caractere;
                MensagemFrente = 0;
            }
        }   
    } 
    else if (caractere == 's') {
        if(*mensagem_s != '\0') {
            UDR0 = *mensagem_s;
            mensagem_s++;
        }
        else {
            mensagem_s = &(msg_s[0]);
            UCSR0B &= ~(0x20);
            charAntigo = caractere;
        }
    }
    else if (caractere == 'a') {
        if(*mensagem_a != '\0') {
            UDR0 = *mensagem_a;
            mensagem_a++;
        }
        else {
            mensagem_a = &(msg_a[0]);
            UCSR0B &= ~(0x20);
            charAntigo = caractere;
        }
    }
    else if (caractere == 'd') {
        if(*mensagem_d != '\0') {
            UDR0 = *mensagem_d;
            mensagem_d++;
        }
        else {
            mensagem_d = &(msg_d[0]);
            UCSR0B &= ~(0x20);
            charAntigo = caractere;
        }
    }
    else if (caractere == 'q') {
        if(*mensagem_q != '\0') {
            UDR0 = *mensagem_q;
            mensagem_q++;
        }
        else {
            mensagem_q = &(msg_q[0]);
            UCSR0B &= ~(0x20);
            charAntigo = caractere;
        }
    }
    
    else if (caractere == 'e') {

        msg_e[0] = a; // coloca o algarismo da centena na posicao 0 do vetor msg_e
      	msg_e[1] = b; // coloca o algarismo da dezena na posicao 1 do vetor msg_e
      	msg_e[2] = c; // coloca o algarismo da unidade na posicao 2 do vetor msg_e
        if(*mensagem_e != '\0') {
            UDR0 = *mensagem_e;
            mensagem_e++;
        }
        else {
            mensagem_e = &(msg_e[0]);
            UCSR0B &= ~(0x20);
            charAntigo = caractere;
        }
    }
    
    else if (caractere == '6') {
        if(*mensagem_6 != '\0') {
            UDR0 = *mensagem_6;
            mensagem_6++;
        }
        else {
            mensagem_6 = &(msg_6[0]);
            UCSR0B &= ~(0x20);
            charAntigo = caractere;
        }
    }
    
    else if (caractere == '8') {
        if(*mensagem_8 != '\0') {
            UDR0 = *mensagem_8;
            mensagem_8++;
        }
        else {
            mensagem_8 = &(msg_8[0]);
            UCSR0B &= ~(0x20);
            charAntigo = caractere;
        }
    }
    
    else if (caractere == '0') {
        if(*mensagem_0 != '\0') {
            UDR0 = *mensagem_0;
            mensagem_0++;
        }
        else {
            mensagem_0 = &(msg_0[0]);
            UCSR0B &= ~(0x20);
            charAntigo = caractere;
        }
    }

    /* Caso o caractere recebido nao corresponda a nenhum
       comando, o caractere anterior eh carregado no caractere atual. */
    else
    {
        caractere = charAntigo;
    }
}

/* Vetor de interrupcao recepcao completa da USART */
/* Uma interrucao eh gerada sempre que ha dados nao lidos no buffer de chegada/entrada. */
ISR(USART_RX_vect) 
{
    caractere = UDR0;
}

/* Vetor de interrupcao por comparacao de TCNT0 com OCR0A. */
ISR(TIMER0_COMPA_vect)
{
    /* Zera o bit2 (PD2) para finalizar o pulso de 50us quando
       PD2 esta setado previamente. */
    PORTC &= ~(0x01);

    /* Contabiliza uma nova interrupcao (50us). */
    contadorTimer0++;

    /* Com 4000 interrupcoes temos 200ms e podemos gerar um novo pulso de 50us
       setando PD2 e zerando logo em seguida com a proxima interrupcao. */
    if (contadorTimer0 >= 4000)
    {
        /* Seta o bit2 (PD2) para gerar/iniciar um pulso de aproximadamente de 50us. */
        PORTC |= 0x01;

        /* Zera o contador de interrupcoes. */
        contadorTimer0 = 0;

        /* Contabiliza +200ms. */
        contador1segundo++;
    }

    /* Quando contador1segundo = 5, temos 5*200ms = 1s,
       e assim uma nova mensagem pode ser enviada. */
    if (contador1segundo >= 5)
    {
        UCSR0B |= 0x20; // Habilita o bit da interrupcao de regsitrador de dados vazio
        contador1segundo = 0; // Zera a variavel de contagem
    }
}

/* Vetor de interrupcao por comparacao relacionado ao Timer/Counter1.  */
ISR(TIMER1_COMPA_vect)
{  
    /* O top de contagem eh atualizado com base na quantidade de
       interrupcoes que o echo possui. TCNT1 eh zerado para evitar
       erros, pois estamos atualizados o top de contagem durante 
       o funcionamento. 
       O fator de correcao eh necessario para gerar um top razoavel. */
    TCNT1 = 0;
    OCR1A = ciclosEcho * multiplicador; 

    /* Alternando a flag eh possivel fazer o led "piscar" entre as interrupcoes. */
    if(flag)
    {
        PORTC |= 0x20; // Acende o led
        flag = 0;
    }
    else
    {
        PORTC &= ~0x20; // Apaga o led
        flag = 1;
    }
}

/* Vetor de interrupcao relacionado ao Echo. 
   Toda diferenca de borda em INT0 (PD2) gera uma interrupcao. */
ISR(INT0_vect)
{
    /* Caso for uma borda de subida, contabilizamos em qual interrupcao ocorreu o inicio do Echo. */
    if (PIND & 0x04)
    {
        inicioEcho = contadorTimer0;
    }
    /* Caso contrario eh salvo a diferenca entre o numero da
       interrupcao de inicio e fim do Echo, assim sendo possivel o
       calculo da distancia, pois cada interrupcao corresponde a 50us.*/
    else
    {
        ciclosEcho = contadorTimer0 - inicioEcho;
        calculo = 1;
    }
}

int main() {

    /* Desabilita a flag global de interrupcao por precaucao. */
    cli();

    /* Configuracao do sinal PWM gerado. */
    setupMotor();
    
    /* Configuracao do modo de operacao da USART. */
    setupUsart();
    
    /* Configuracao do Timer1 para controlar a
       frequencia de funcionamento do LED. */
    setupLed();
    
    /* Configuracao do Timer0 para ser usado com o sensor ultra sonico. */ 
    setupSensor();

    /* Ativa as interrupcoes globais. */
    sei();

    /* A interrupcao de 'registrador de dados vazio' eh habilitado. */
    UCSR0B |= 0x20;
    
    /* loop infinito */
    while(1) {

        /* Delay com o objetivo de acelerar a simulacao. */
        _delay_ms(1);

        if (calculo)
        {
            /* Funcao para calcular a distancia a partir da quantidade de ciclos durante o Echo. */
            calculoDistancia();

            /* Funcao para converter a distancia de micro metro para centrimetros em codigo ascii. */
            conversao();
            calculo = 0; // zera a flag do calculo da distancia
        }

        /* Rotina para alterar o modo de operacao dos motores. */
        comandoMotores();
    }
}

/* funcao de calculo da distancia - sabendo que o periodo do pulso de 50us e a velocidade do som sao constantes, entao fizemos o seguinte calculo: 
   343*50us e divide essa expressao por 2, obtendo 8575 micrometros por interrupcao. A unidade dessa constante eh convertida para cm na funcao conversao.

   distancia = 343 * (1/2) * T = 343 * (1/2) * (ciclosEcho * 50 [us]) = 8575 * ciclosEcho [micro metro] */
void calculoDistancia(void)
{
    distancia = 8575 * (unsigned long int) ciclosEcho; // O cast eh necessario para nao ocorrer erros na conta
    
    /* Quando a distancia for menor ou igual a 10cm, entao a flag obstaculo eh setada */
    if (distancia <= 100000)
    {
        obstaculo = 1;
    }
    /* Caso contrario, a flag eh zerada */
    else
    {
        obstaculo = 0;
    }
}

/* funcao de conversao de decimal para codigo ascii */
void conversao(void)
{
    /* Como o valor da distancia ira ser alterado dentro da funcao, de modo que cada algarismo do 
       numero da distancia seja separado para realizar a conversao. As variaveis 'a', 'b' e 'c' 
       representam a centena, dezena e unidade, respectivamente */
    valor = distancia; // Salva a distancia em uma variavel diferente para poder manipular o valor
    a = valor/1000000; // variavel que retira o algarismo que represeta a centena da distancia em cm
    valor = valor - (a*1000000); // Decrementa o valor retirado
    b = valor/100000; // variavel que retira o algarismo que represeta a dezena da distancia em cm
    valor = valor - (b*100000);
    c = valor/10000; // variavel que retira o algarismo que represeta a unidade da distancia em cm
    
    /* depois de dividir o numero em algarismos individuais, eh somado 48 ou 0x30, que eh a diferenca entre
       o numero normal e o numero segundo a tabela do codigo ascii. Feito a conversao, eh colocado esses algarismos
       dentro do vetor da msg_e nas posicoes 0, 1 e 2. Isso eh feito dentro da interrupcao da USART */
    a = a + 48;
    b = b + 48;
    c = c + 48;
}

/* Funcao para alterar o modo de operacao dos motores. */
void comandoMotores(void)
{
    /* Comando para frente */
    /* Caso o comando 'e' esteja a ativo e o comando para frente,
       a condição também será avaliada. */
    if (caractere == 'w' || caractere == 'e' && (PORTC & ~0x1E))
    {
        /* caso a distancia for menor ou igual a 10cm, os motores devem parar e nao executar o comando para frente.
           Os outros comando podem ser executados normalmente. */
        if (obstaculo) {
            /* A1, A2, A3 e A4 em nivel baixo */
            PORTC &= ~0x1E;
        }
        /* Caso contrario, executa normalmente o comando de se movimentar para frente. */
        else {
            /* Anula o comando anterior zerando o bits PC1, PC2, PC3 E PC4. */
            PORTC &= ~0x1E;

            /* A1 e A3 em nivel alto*/
            PORTC |= 10;
        }
    }

    /* Para tras */
    else if (caractere == 's')
    {
        PORTC &= ~0x1E;

        /* A2 e A4 em nivel alto */
        PORTC |= 20;
    }

    /* Anti-horario */
    else if (caractere == 'a')
    {
        PORTC &= ~0x1E;

        /* A1 e A4 em nivel alto */
        PORTC |= 18;
    }

    /* Horario */
    else if (caractere == 'd')
    {
        PORTC &= ~0x1E;

        /* A2 e A3 em nivel alto */
        PORTC |= 12;
    }

    /* Parado */
    else if (caractere == 'q')
    {
        /* A1, A2, A3 e A4 em nivel baixo */
        PORTC &= ~0x1E;
    }

    /* Duty-cycle em 60% */
    else if (caractere == '6')
    {
        OCR2B = 153;
    }

    /* Duty-cycle em 80% */
    else if (caractere == '8')
    {
        OCR2B = 204;  
    }

    /* Duty-cycle em 100% */
    else if (caractere == '0')
    {
        OCR2B = 255;
    }
}
