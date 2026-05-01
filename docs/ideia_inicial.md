Você é um engenheiro de software que estuda na 42. Procure pelo projeto de webserv no currículo básico da escola 42 (42 School). Eu faço parte de um grupo que precisa implementar este projeto. Preciso criar um plano para me ajudar na implementação, de uma forma que eu consiga separar no futuro as tarefas para mim e para os outros dois membros do grupo. Preciso criar um bom plano de implementação para nos guiar na execução do projeto.

Preciso começar o projeto, de uma forma simples para que todos tenham um código de origem para criarem as suas branches, e começarem seus desenvolvimentos paralelamente.
Essa estrutura inicial deve ser simples, para que seja criada o mais rapidamente possivel, ela servirá apenas para que todos iniciem suas branches, e não tenhamos conflitos para fazer merge conforme as tarefas ficam prontas.

Explique no plano os motivos de ter escolhido cada Design Pattern, o que exatamente eles facilitam nesse projeto? Seja bem detalhado no seu plano. Principalmente nos Headers do projeto, eu quero ter uma estrutura de headers com definições de funções que façam sentido para o projeto.

Logo abaixo eu tenho uma ideia de plano que já pensei, porém preciso que você analise esse plano, de forma crítica, com um olhar técnico bem voltado para bboas práticas de desenvolvimento. E Também lembrando que preciso fazer isso com mais 2 pessoas, ou seja, preciso que o código seja bem modularizado e com injeções claras de dependências para que todos consigam realizar suas tarefas de forma paralela sem afetar o código do outro.

Caso tenha alguma dúvida para criar o plano oficial, me pergunte, NÃO DEDUZA NENHUMA INFORMAÇÃO EU QUERO TER CONTROLE TOTAL DE TODAS AS DECISÕES PARA A CRIAÇÃO DO PLANO.

---

Fase 1: Arquitetura Inicial 

Definição de Estruturas de Dados: Vocês precisam definir juntos as classes principais (ex: Server, Client, Request, Response, Config) para que o código de um membro possa interagir com o do outro.

Fase 2: Divisão Principal de Tarefas
Pela natureza do webserv, o projeto pode ser dividido em três pilares principais:
Membro 1: Core de Rede e I/O Multiplexing (O "Motor")
Responsável por gerenciar as conexões e garantir que o servidor não trave.
Sockets Não-bloqueantes: Configurar os sockets do servidor e dos clientes para o modo não-bloqueante (usando fcntl com O_NONBLOCK).
Loop Principal com select(): Implementar o loop central usando apenas um select() para monitorar múltiplos sockets de leitura e escrita simultaneamente. Nunca se deve ler ou escrever sem passar pelo select().
Gestão de Clientes: Criar a lógica para aceitar novas conexões (accept()), rastrear o estado de cada cliente conectado e lidar de forma segura com desconexões ou conexões interrompidas.
Recepção/Envio Parcial: Lidar com envios e recebimentos de dados parciais. O recv() ou send() pode não ler/escrever todos os bytes de uma vez, exigindo buffers acumulativos.
Membro 2: Parsers (A "Tradução")
Responsável por transformar texto puro em objetos manipuláveis em C++.
Parser do Arquivo de Configuração: Criar um leitor para o arquivo de configuração estilo Nginx. Isso inclui analisar diretivas como server, listen, server_name, blocos de location, páginas de erro personalizadas (error_page), limite de tamanho do corpo (client_max_body_size) e diretivas de roteamento.
Parser de Requisição HTTP (Request): Construir a máquina de estados que lê a string bruta recebida do socket e a divide em:
Método (GET, POST, DELETE), URI e Versão HTTP.
Headers (armazenados em um formato como std::map com chaves case-insensitive).
Body (lidando com conteúdos com Content-Length ou formato Transfer-Encoding: chunked).
Membro 3: Lógica HTTP, Respostas e Arquivos (O "Cérebro")
Responsável por decidir o que fazer com a requisição e montar a resposta para o cliente.
Roteamento: Unir a requisição analisada pelo Membro 2 com as regras de configuração. Decidir qual servidor virtual responderá (baseado no header Host e na porta) e aplicar as regras do bloco location correto.
Métodos HTTP: Implementar a lógica para tratar métodos (GET, POST e DELETE). Permitir servir arquivos estáticos (.html, imagens) com os MIME types corretos, tratar diretórios (servindo arquivos index ou gerando listagem de diretório/autoindex) e tratar a remoção de arquivos.
Construtor de Respostas (Response): Gerar a string de resposta HTTP formatada de acordo com as especificações da RFC (Status Line, Headers corretos como Content-Type e Content-Length, Body). Lidar com páginas de erro adequadas (404, 403, 400, etc.).


Fase 3: A Integração do Desafio Maior – O CGI (Common Gateway Interface)
A interface CGI permite que o servidor execute scripts externos (como Python ou PHP) de forma dinâmica. Este componente é muito complexo e sugere-se que seja implementado conjuntamente, dividindo assim:
Membro 2 (Parser/Config): Identificar na configuração quais extensões acionam o CGI (ex: .php ou .py) e extrair informações cruciais da URI para passar ao script.
Membro 3 (Lógica de Requisição): Preparar as variáveis de ambiente necessárias para o script CGI e tratar a entrada de dados (como um formulário via POST) usando os cabeçalhos corretos (CONTENT_LENGTH, CONTENT_TYPE, etc).
Membro 1 (Core/Rede): Fazer a execução em si através de fork(), mapear os file descriptors usando pipes para ler a saída padrão do script e integrar com o select() para que o servidor principal não fique travado esperando um CGI demorado terminar de processar.

Fase 4: Bateria de Testes (Todos)
O projeto Webserv possui checklists de correção extremamente rigorosos. A fase final deve envolver todos os membros quebrando o próprio servidor:
Siege / Stress Testing: Utilizar a ferramenta siege (ex: siege -c50 -t30s) para testar o servidor sob alta concorrência e garantir que ele não trave e gerencie o tráfego de maneira eficiente.
Memory Leaks: Como o projeto deve compilar sem vazar memória, rodar com ferramentas como leaks (no macOS) ou valgrind durante requisições repetitivas ou conexões canceladas inesperadamente.
Testes de Compatibilidade de Navegador: Conectar pelo Chrome, Safari, etc, e garantir que as páginas renderizem corretamente e persistam conexões se possível.
Casos Críticos (Edge Cases): Enviar requisições malformadas, requisições cujo corpo excede o tamanho limite configurado (413 Payload Too Large), tentar deletar diretórios proibidos (403 Forbidden) ou efetuar uploads de arquivos sem usar o método correto.
Trabalhando desta forma, os membros dependem do bom planejamento das interfaces entre cada parte, mas não precisam cruzar muito os códigos durante o desenvolvimento. Desejo um ótimo projeto a você e ao seu grupo!

---

Para começar o projeto rapidamente e permitir que os três membros trabalhem em paralelo sem gerar conflitos de merge, a melhor estratégia é adotar uma abordagem baseada em "Contratos" (Contract-First) focada na criação apenas dos arquivos de cabeçalho (.hpp).
O segredo de projetos de sucesso no Webserv da 42 é estabelecer as definições de classes logo na Fase 1, permitindo que a equipe trabalhe em paralelo com o mínimo de atrito.
Abaixo estão os padrões de projeto (Design Patterns) mais encontrados em implementações de sucesso do Webserv e como você deve estruturar o código inicial.
Padrões de Desenvolvimento (Design Patterns) Recomendados
State Machine (Máquina de Estados) Este é o padrão mais crítico para o Webserv, sendo expressamente necessário para lidar com conexões não-bloqueantes onde os dados chegam fragmentados.


Onde usar: O Membro 1 e o Membro 2 usarão isso. Vocês devem criar uma classe Client que possua uma variável de estado (ex: READING_HEADERS, READING_BODY, GENERATING_RESPONSE, SENDING_RESPONSE). O parser de requisição também usa estados como REQUEST_METHOD, HEADER_KEY, BODY, etc.
Reactor Pattern (Loop de Eventos Central) Este padrão é o coração do servidor. Ele inverte o controle: em vez do código ficar esperando os dados chegarem, um loop central (usando poll ou select) monitora os eventos e "despacha" a execução para o tratador correto quando um socket está pronto.


Onde usar: O Membro 1 usará na classe Server. O loop central apenas delega: se o evento for POLLIN num socket de cliente, ele chama a função de leitura; se for POLLOUT, chama a função de escrita.
Data Transfer Object (DTO) / Wrapper Serve para passar dados entre os módulos de forma limpa, sem expor a lógica interna. O parser de configuração, por exemplo, não deve entregar um dicionário complexo, mas sim um objeto encapsulado e tipado.


Onde usar: Criação das classes Request, Response e ServerConfig.

O Plano de Ação: Criando o Código de Origem (Esqueleto)
Para que todos iniciem suas branches hoje, vocês três devem se reunir e escrever apenas os arquivos .hpp. Vocês não vão implementar a lógica (.cpp) agora. Apenas definirão as "peças do quebra-cabeça".
A estrutura inicial no repositório main deve conter:
1. Config.hpp (Membro 2)
Define a estrutura que guardará as regras do arquivo .conf. Todos os módulos vão consultar essa classe.
class ServerConfig {
public:
    int listen_port;
    std::string server_name;
    std::string root;
    // ... outros atributos básicos
};

2. Request.hpp e Response.hpp (Membros 2 e 3)
O Motor de Rede (Membro 1) passará um buffer bruto para o Parser (Membro 2), que deve retornar um objeto Request padronizado. Este objeto será lido pelo Construtor de Respostas (Membro 3).
class Request {
public:
    std::string method;
    std::string uri;
    std::map<std::string, std::string> headers;
    std::string body;
    // status de parsing (completo, incompleto, erro)
};

class Response {
public:
    int status_code;
    std::map<std::string, std::string> headers;
    std::string body;

    std::string toString(); // Transforma o objeto na string HTTP final para o Membro 1 enviar
};

3. Client.hpp (Membro 1)
Gerencia o estado de cada conexão para que o select() ou poll() não se perca.
class Client {
public:
    int socket_fd;
    int state; // Usando o padrão de Máquina de Estados (READING, WRITING)
    std::string raw_request_data;
    Request     parsed_request;
    Response    response_to_send;
};

4. Server.hpp (Membro 1)
A classe que inicializa os sockets e roda o Reactor Pattern.
class Server {
public:
    void init(std::vector<ServerConfig> configs);
    void run(); // O loop infinito com select() ou poll()
};

Como trabalhar a partir desse esqueleto:
Façam o commit inicial: Subam esses arquivos .hpp (mesmo que vazios de lógica) para a branch main.
Criação das Branches:
O Membro 1 cria a branch network-core e começa a implementar Server.cpp e Client.cpp criando sockets e o loop do poll() / select(). Para testar, ele pode criar objetos Response manuais e falsos (mock).
O Membro 2 cria a branch parsers e implementa a lógica do Config.cpp e da máquina de estados do Request.cpp. Ele não precisa da rede pronta; pode testar o parser passando strings puras copiadas de requisições HTTP.
O Membro 3 cria a branch http-logic e foca no roteamento e arquivos. Ele finge que já recebeu um objeto Request preenchido e constrói o Response adequado de acordo com as regras.
Trabalhando baseados apenas nesses contratos (os .hpp), vocês garantem que quando o Membro 2 terminar de fazer o parser da Request, o Membro 3 já terá um código esperando exatamente aqueles dados, e o Membro 1 só precisará chamar a função response.toString() para jogar no socket e enviar ao cliente. Isso zera os conflitos de integração.

