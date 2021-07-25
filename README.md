# OS_Project

2020-2 Operating System Term Project

Round Robin Scheduling by Message Queue in linux

# 구현 내용

Time tick, quantum을 정했다. **Tick은 10ms**로 정했고 **Quantum은 tick의 배수**로 정할 것이다. 

사용할 구조체를 정의했다. Process 구조체에는 PID, CPU/IO Burst, 남은 CPU/IO Burst/ 그리고 통계를 위한 waiting time, response time이 선언되어 있다. 시뮬레이션 결과 확인을 위한 simulData 구조체에는 전체 프로세스의 개수, waiting time, turnaround time, response time, context switch 횟수가 선언되어 있다. Ready queue, waiting queue를 위한 queue 구조체는 doubly linked list로 구현했다. 

Argument를 받을 수 없는 시그널 핸들러를 위해 전역 변수를 선언했다. 자식 프로세스의 메모리 낭비를 줄이기 위해 ready queue, waiting queue, simulation data 포인터를 선언한다. 10,000 tick동안 시뮬레이션을 할 예정이기 때문에 이를 확인할 수 있도록 tick_counter를 선언했다. 정해놓은 quautum이 지나면 cpu 사용 권한을 다른 프로세스에게 넘겨주어야 하기 때문에 이를 기록하기 위한 quantum_counter를 선언했다.

프로그램 시작 시각을 확인하기 위해 start 변수를 선언했다. Tick이 10ms이고 이를 통해 tick counter로 프로그램 진행 시간을 확인할 수 있지만 컴퓨터 사양이나 사용 환경에 따라 tick이 delay되는 경우도 있기 때문에 정확한 확인을 위해 선언하였다. 밀리세컨드 단위로 기록해야 하기 때문에 long long 자료형을 사용했다. 

부모 프로세스와 자식 프로세스간 통신을 위한 메시지 큐를 생성했다. 한개의 메시지 큐를 사용할 것이기 때문에 KEY를 사전에 정해 놓았다.  

부모 프로세스는 10개의 자식 프로세스를 생성한다. 이때 단순히 fork()를 반복문으로 10번 실행하면 자식 프로세스 또한 fork()를 실행하기 때문에 getpid()를 이용하여 부모 프로세스의 경우에만 fork()를 하도록 했다. 

## 자식 프로세스의 경우
각각의 자식 프로세스는 자신만의 process 구조체를 생성하고 init_process 함수를 통해 랜덤하게 cpu burst를 정한다. 이 구조체를 message에 담아서 부모 프로세스에게 전달하는데, message type를 본인의 진짜 pid로 하여 부모 프로세스가 어떤 자식 프로세스한테서 온 메시지인지 확인할 수 있도록 한다.

메시지 전송 후 자식 프로세스는 부모 프로세스한테서 메시지가 올 때까지 대기한다. Msgrcv 함수의 argument로 IPC_NOWAIT 대신 0을 주면 메시지가 올 때까지 기다린다. 메시지 타입이 자신의 pid + MAX_PID(32768) 인 메시지만 수신하도록 한다. MAX_PID를 더한 이유는 자신의 pid를 메시지 타입으로 하는 메시지를 받을 때까지 대기하도록 하면, 부모 프로세스에게 메시지를 보내자 마자 다시 바로 메시지를 받아 버리기 때문이다. 

tick마다 전송되는 메시지를 받아 자신의 process 정보를 업데이트 한다. 만약 이때 남은 cpu burst와 io bust가 모두 0 이하라면, init process 함수를 통해 다시 프로세스 구조체를 초기화하고, 이를 다시 부모 프로세스에게 전송한다. 이때 메시지 타입은 자신의 pid이다. 즉 자식이 부모에게 보내는 메시지의 메시지 타입은 자기 pid이고, 부모가 자식에게 보내는 메시지의 메시지 타입은 해당 자식 프로세스의 pid + MAX_PID이다. 

## 부모 프로세스의 경우
부모 프로세스가 ready queue와 waiting queue, simulation data를 관리해야 하기 때문에 이를 모두 변수로 선언하고, 위에서 포인터로 선언했던 전역 변수와 연결시켜준다. 
일정 시간마다 (정해놓은 TICK마다) 시그널을 받으면 시그널 핸들러를 실행하도록 한다. 받을 시그널은 SIGVTARM이다. 

## 시그널 핸들러 
종료조건은 프로그램이 10,000 TICK 돌았을 때이다. 혹시 메시지 큐에서 아직 전송중인 메시지가 있을 수 있기 때문에 받아들이고, 큐에 담긴 프로세스의 pid로 SIGKILL 시그널을 보내 종료시킨다. 이후 큐의 노드로 사용된 메모리를 반환한다. 시뮬레이션 결과를 출력하고 생성한 메시지 큐까지 삭제한 이후 부모 프로세스를 종료하도록 하였다. 

시그널 핸들러가 실행되면 우선 자식 프로세스가 보낸 메시지 큐에 쌓여있는 메시지를 받는다. 이 때 메시지 타입이 MAX_PID보다 작은 메시지만 받기 위해 (자식 프로세스가 보낸 메시지만 받기 위해) 받을 메시지 타입을 –MAX_PID로 설정했다. 프로세스에 arrival time을 기록한다. 기록 후 ready queue에 enqueue한다.

Ready queue가 비어있지 않은 경우 다음과 같은 작업을 수행한다

cpu를 할당받은 ready queue의 front에 위치한 프로세스를 제외하고 다른 ready queue에 존재하는 모든 프로세스의 waiting time을 tick만큼 증가시킨다.

cpu를 할당받은 ready queue의 front에 위치한 프로세스의 남은 cpu burst를 tick만큼 감소시킨다

만약 ready queue의 front에 위치한 프로세스의 response 값이 -1이라면 (즉 이번이 처음 cpu를 할당받은 것이라면) 할당받는데 걸린 시간을 기록한다.

만약 ready queue의 front에 위치한 프로세스의 남은 cpu burst가 0이거나, 혹은 time quantum이 지났다면 1/2 확률로 IO 요청을 받도록 구현했다. IO 요청을 받은 경우 ready queue에서 dequeue되고 waiting queue로 enqueue 된다. IO 요청을 받지 못한 경우, 남은 cpu burst 여부에 따라 ready queue의 맨 뒤로 가거나 (quantum 지난 경우), dequeue 된다. Dequeue 되는 경우 turnaround time을 계산하고 기록된 response time, waiting time을 포함하여 모두 simulation data에 추가한다. 

cpu burst가 감소하고, IO 요청을 받아 변화한 프로세스 구조체의 내용을 자식 프로세스에게 전달한다. 

node가 waiting queue를 순회하며 모든 프로세스의 남은 IO burst를 tick만큼 감소시키고, 이를 해당하는 자식 프로세스에게 전송한다. (모두 다른 IO 하고 있다고 가정)

IO burst와 cpu burst가 모두 0 이하가 된 경우, turnaround time을 계산하고 기록된 response time, waiting time을 포함하여 모두 simulation data에 추가한다. 이후 queue에서 해당하는 node를 제거하고, 연결해준다. IO burst만 0이 되고 cpu burst는 남은 경우(quantum이 지나고 IO를 요청받아 waiting queue로 온 경우) 다시 ready queue에 enqueue 시켜준다. 

현재 몇번째 TICK인지 출력하고, ready queue와 waiting queue에 어떤 프로세스가 있는지, 얼마나 burst가 남았는지, 언제 도착했는지 출력해준다. 이 모든 과정이 끝나면 남은 burst가 변화한 모든 프로세스는 부모로부터 메시지를 받아 업데이트가 된다. 
 

# 시뮬레이션 결과 확인
![image](https://user-images.githubusercontent.com/6195873/126889999-2a4bbb31-8a75-45d9-bcb8-e071e3e39592.png)

Quantum이 줄어들면 평균 응답 시간은 짧아지지만 문맥 교환이 매우 많이 일어난다. 
Quantum이 늘어나면 FCFS에 가까워져 평균 응답 시간이 늘어난다. 
