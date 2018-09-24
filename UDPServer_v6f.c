//####################################################################
//                              MEMO
//####################################################################
//FCM導入方法：https://qiita.com/outerlet/items/e3fe96a3d1e15ed42573
//API_KEYには、↑を参照し、挿入してください。
//
//コンパイルコマンド：gcc -Wall -o UDPServer_v6f UDPServer_v6f.c -lmysqlclient -L/usr/lib64/mysql -lcurl
//実行コマンド：./UDPServer_v6f       or      ./UDPServer_v6f [サーバが待ち受けるport番号]
//
//使用したデータベース環境
//DBSERVER:X.X.X.X
//DBNAME:データベース名
//DBUSER:データベースユーザ名
//PASS:データベースユーザパスワード
//テーブル：データベーステーブル名
//※使用するためには、↑のデータベース環境を作成しておく必要がある。(変更可能)
//
//####################################################################


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <mysql/mysql.h>
#include <unistd.h> //sleep,getpid警告用
#include <curl/curl.h>

#define DEBUG

#define DFSPORT 9000 //デフォルト使用ポート（変更可能）
#define ECHOMAX 512
#define API_KEY "API_KEY"

/*関数宣言*/
void DieWithError(char *errorMessage);
void UseIdleTime();
void SIGIOHandler(int signalType);
void Reception();
void ChangeToken();
void DatabaseOpen();
void Registration();
int OverlapUser(int phone);
void Message();
void Search();
void Delete();
void Update();

/*グローバル関数*/
int sock;
char echoBuffer[ECHOMAX]; //文字列用バッファ
int recvMsgSize; //受信メッセージサイズ
struct sockaddr_in echoClntAddr; //クライアントの構造体
unsigned int cliAddrLen; //アドレス長
char *pos;
char *name;
char *sphone;
char *token;
char sql_strexe[256];

/*MYSQL用関数*/
MYSQL *conn = NULL;
MYSQL_RES *resp = NULL;
MYSQL_ROW row;
char *sql_serv = "X.X.X.X"; //データベースを使用するIP
char *user = "データベースユーザ名"; //データベースを使用するユーザ名
char *passwd = "パスワード"; //データベースを使用するユーザのパスワード
char *db_name = "データベース名"; //データベースで使用するデータベース名

unsigned int id = 1000; //ID(デフォルト1000から始める)


/*Main関数---------------------------------------------------------*/
int main(int argc,char *argv[]){
	unsigned short echoServPort;
	struct sockaddr_in echoServAddr;
	struct sigaction handler;

	switch(argc){
		case 1:
			echoServPort = DFSPORT;break;
		case 2:
			echoServPort = atoi(argv[1]);
			if(echoServPort < 1024){
				fprintf(stderr,"ウェルノウンポートなので使えません\n1024以降をお使いください\n");
			}
			break;
		default:
			fprintf(stderr,"使用方法:%s <待ち受けポート番号> \n",argv[0]);
			exit(-1);
	}
	printf("簡易UDP_CHATサーバ:ポート番号%dで待ち受けます\n\n",echoServPort);

	/*ソケット作成*/
	if((sock = socket(PF_INET,SOCK_DGRAM,IPPROTO_UDP)) < 0){
    DieWithError("socket()失敗\n");
  }
	/*ローカルアドレスの構造体を作成*/
  memset(&echoServAddr,0,sizeof(echoServAddr));
  echoServAddr.sin_family = AF_INET;
  echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY); //INADDR_ANYだとどれでも要求を受け付ける
  echoServAddr.sin_port = htons(echoServPort);
	/*ローカルアドレスへバインド(ソケットを名前をつける)*/
  if(bind(sock,(struct sockaddr *) &echoServAddr,sizeof(echoServAddr)) < 0){
    DieWithError("bind()失敗\n");
  }
	/*SIGIOのシグナルハンドラを設定*/
  handler.sa_handler = SIGIOHandler;
  /*全シグナルをマスクするマスクを作成*/
  if(sigfillset(&handler.sa_mask) < 0){
    DieWithError("sigfillset() 失敗\n");
  }
  /*フラグなし*/
  handler.sa_flags = 0;
  /*SIGIOシグナルの処理方法を設定*/
  if(sigaction(SIGIO,&handler,0) < 0){
    DieWithError("sigaction() 失敗　for SIGIO\n");
  }
  /*ソケットを所有してSIGIOメッセージを受信させる*/
  if(fcntl(sock,F_SETOWN,getpid()) < 0){
    DieWithError("fcntl() 失敗　");
  }
  /*ノンブロッキングIOとSIGIO送信セットアップ*/
  if(fcntl(sock,F_SETFL,O_NONBLOCK | FASYNC) < 0){
    DieWithError("クライアントのソケットをノンブロックすることができない\n");
  }
  /*処理を離れてほかの仕事をする*/
  while(1){
    UseIdleTime();
  }
  /*ここには到達しない*/
}

/*クライアントからの入出力を検知--------------------------------------*/
void SIGIOHandler(int signalType){
	char mode = '0';
	do{
		/*入力パラメータのサイズをセット*/
		cliAddrLen = sizeof(echoClntAddr);
		Reception();

		if(recvMsgSize < 0){ //ゴミは表示されるため
      memset(echoBuffer,'\0',recvMsgSize);
      continue;
    }

		mode = echoBuffer[0];
		switch(mode){
			case '0':
				printf("デバイストークン管理処理\n");
				ChangeToken();break;
			case '1':
				printf("ユーザー登録\n");
				Registration();break;
			case '2':
				printf("メッセージ処理\n");
				Message();break;
			case '3':
				printf("ユーザー検索処理\n");
				Search();break;
			case '4':
				printf("ユーザー消去処理\n");
				Delete();break;
			case '5':
				printf("ユーザー変更処理\n");
				Update();break;
			default:
				printf("失敗\n");break;
		}
	}while(recvMsgSize >= 0);
}

/*デバイストークン管理-------------------------------------------------*/
void ChangeToken(){
	char *pos;
	char *cid;
	char *token;
	int id = 0;
	int i = 1;

	pos = strtok(echoBuffer,"/");
	pos = strtok(NULL,"/");
	while(pos != NULL){
		switch(i){
			case 1:
				if((cid = (char *)malloc(sizeof(pos))) == NULL ){
          DieWithError("cidメモリが確保できませんでした\n");
        }
				strcpy(cid,pos);
				id = atoi(cid);break;
			case 2:
				if((token = (char *)malloc(sizeof(pos)+256)) == NULL){
          DieWithError("tokenメモリが確保できませんでした\n");
        }
        strcpy(token,pos);break;
			default:
				break;
		}
		pos = strtok(NULL,"/");
		i++;
	}
	#ifdef DEBUG
  printf("id:%d\ntoken%s\n",id,token);
  #endif

	DatabaseOpen();
	snprintf(&sql_strexe[0],sizeof(sql_strexe)-1,"UPDATE データベーステーブル名 SET token = '%s' where id = %d",token,id);
	free(cid);free(token);
	if(mysql_query(conn,&sql_strexe[0])){
    DieWithError("ユーザー変更要求に失敗\n");
  }
  mysql_close(conn);
}

/*ユーザ登録処理-------------------------------------------------------*/
void Registration(){
	int phone = 0;
	int i = 1;
	char Srecvid[256];
  char *recvid = NULL;
  int recvidlen = 0;
	int result = 0;
  char *overlap = "444";
  int overlaplen = 0;

	pos = strtok(echoBuffer,"/");
  pos = strtok(NULL,"/");

	while(pos != NULL){
		switch(i){
			case 1:
				if((name = (char *)malloc(sizeof(pos))) == NULL){
          DieWithError("nameメモリが確保できませんでした\n");
        }
				strcpy(name,pos);break;
			case 2:
        if((sphone = (char *)malloc(sizeof(pos))) == NULL){
          DieWithError("sphoneメモリが確保できませんでした\n");
        }
        strcpy(sphone,pos);
        phone = atoi(sphone);break;
      case 3:
        if((token = (char *)malloc(sizeof(pos)+256)) == NULL){
          DieWithError("tokenメモリが確保できませんでした\n");
        }
        strcpy(token,pos);break;
      default:
        break;
		}
		pos = strtok(NULL,"/");
    i++;
	}
	result = OverlapUser(phone); //重複確認処理
	if(result == 1){
    /*重複のためそのことをクライアントに返す*/
    overlaplen = strlen(overlap);
    sendto(sock,overlap,overlaplen,0,(struct sockaddr *)&echoClntAddr,sizeof(echoClntAddr));
    #ifdef DEBUG
    printf("重複しているため444を返す\n");
    #endif
  }else{
    /*ここからデータベース処理*/
    DatabaseOpen();

    snprintf(&sql_strexe[0],sizeof(sql_strexe)-1,"INSERT INTO データベーステーブル名(name,phone,token,id)values('%s',%d,'%s',%d);",name,phone,token,id);
    #ifdef DEBUG
    printf("ユーザ名:%s\n電話番号:%d\nデバイストークン:%s\nID:%d\n",name,phone,token,id);
    #endif
    free(name);free(sphone);free(token);

    if(mysql_query(conn,&sql_strexe[0])){
      mysql_close(conn);
      DieWithError("データ登録失敗\n");
    }

		/*mysql接続終了*/
    mysql_close(conn);
    snprintf(&Srecvid[0],sizeof(Srecvid)-1,"%d",id);
    recvid = Srecvid;
    recvidlen = strlen(recvid);

    /*IDをクライアントに返す*/
    sendto(sock,recvid,recvidlen,0,(struct sockaddr *)&echoClntAddr,sizeof(echoClntAddr));
    id = id + 1;
  }
}

/*重複処理-------------------------------------------------------------*/
int OverlapUser(int phone){
	DatabaseOpen();
  int result = 0;

	snprintf(&sql_strexe[0],sizeof(sql_strexe)-1,"SELECT phone FROM データベーステーブル名 WHERE phone = %d",phone);
  if(mysql_query(conn , &sql_strexe[0])){
    mysql_close(conn);
    DieWithError("サーチ失敗\n");
  }
	resp = mysql_use_result(conn);
  if((mysql_fetch_row(resp)) != NULL){
    result = 1;
  }

	/*mysql接続終了*/
  mysql_close(conn);
  #ifdef DEBUG
  printf("重複処理(0:重複なし 1:重複あり): %d\n",result);
  #endif
  return result;
}

/*メッセージ処理-------------------------------------------------------*/
void Message(){
	char *message;
	int messagelen;
  char *CSendid;
  char *CDestid;
  char *check = "1";
  int checksize = strlen(check);
  int Sendid;
  int Destid;
	char DestToken[256]; //宛先トークン
  char SendMessage[512];
  char SendNotification[512];

	pos = strtok(echoBuffer,"/");
  pos = strtok(NULL,"/");
	message = pos;
  messagelen = strlen(message);
  pos = strtok(NULL,"/");
  CSendid = pos;
  pos = strtok(NULL,"/");
  CDestid = pos;
  pos = strtok(NULL,"/");

  Sendid = atoi(CSendid);
  Destid = atoi(CDestid);

	#ifdef DEBUG
  printf("メッセージ: %s\n",message);
  printf("送信元ID: %d\n",Sendid);
  printf("宛先ID: %d\n",Destid);
  #endif
	/*受信したことメッセージをクライアントに返信*/
  sendto(sock,check,checksize,0,(struct sockaddr *)&echoClntAddr,sizeof(echoClntAddr));

	/*データベース処理↓*/
  DatabaseOpen();
  snprintf(&sql_strexe[0],sizeof(sql_strexe)-1,"SELECT token from データベーステーブル名 WHERE id = %d",Destid);
  if(mysql_query(conn , &sql_strexe[0])){
    mysql_close(conn);
    DieWithError("サーチ失敗\n");
  }
  resp = mysql_use_result(conn);
  while((row = mysql_fetch_row(resp)) != NULL){
    snprintf(&DestToken[0],sizeof(DestToken)-1,"%s",row[0]);
  }

	/*msql接続終了*/
  mysql_free_result(resp);
  mysql_close(conn);

//#####################################################################
//プッシュ通知とメッセージを一緒に送信させる↓
//#####################################################################
	/*MessageData送信処理*/
  snprintf(&SendMessage[0],sizeof(SendMessage)-1,"{\"to\":\"%s\",\"data\":{\"subject\":\"%s\",\"text\":\"%s\"}}",DestToken,CSendid,message);

  CURLcode ret;
  CURL *curl;
  curl = curl_easy_init(); //セッション開始
  struct curl_slist *headers = NULL;

  headers = curl_slist_append(headers,"Authorization: key="API_KEY);
  headers = curl_slist_append(headers,"Content-Type:application/json");
  headers = curl_slist_append(headers,"charset:UTF-8");

  curl_easy_setopt(curl,CURLOPT_URL,"https://fcm.googleapis.com/fcm/send");
  curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
  curl_easy_setopt(curl,CURLOPT_POSTFIELDS,SendMessage);

  ret = curl_easy_perform(curl); //実行
	curl_easy_cleanup(curl); //セッション終了

	/*Message通知処理(プッシュ通知)*/
  snprintf(&SendNotification[0],sizeof(SendNotification)-1,"{\"to\":\"%s\",\"notification\":{\"title\":\"タイトル\",\"body\":\"%s\"}}",DestToken,message);
  curl = curl_easy_init(); //セッション開始
  curl_easy_setopt(curl,CURLOPT_URL,"https://fcm.googleapis.com/fcm/send");
  curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
  curl_easy_setopt(curl,CURLOPT_POSTFIELDS,SendNotification);

  ret = curl_easy_perform(curl); //実行

  curl_slist_free_all(headers); //リスト開放
  curl_easy_cleanup(curl); //セッション終了
}

/*ユーザ検索処理-------------------------------------------------------*/
void Search(){
	char *sid;
	int searchid = 0;
  int searchphone = 0;
  char *searchinfo; //name,id
  int searchinfolen;
  char sendName[256] = "データはありません";
  int searchQ = 0; //0:id 1:phone

	pos = strtok(echoBuffer,"/");
  pos = strtok(NULL,"/");
	searchQ = atoi(pos);
  pos = strtok(NULL,"/");
	char sendId[256] = "0";

	DatabaseOpen();

	switch(searchQ){ //0:ID 1:PHONE
		case 0:
			sid = (char *)malloc(sizeof(pos));
      if(sid == NULL){
        DieWithError("sidメモリが確保できませんでした\n");
      }
      strcpy(sid,pos);
      pos = strtok(NULL,"/");
      searchid = atoi(sid);
      #ifdef DEBUG
      printf("searchid : %d\n",searchid);
      #endif
      snprintf(&sql_strexe[0],sizeof(sql_strexe)-1,"select name,id from データベーステーブル名 where id = %d",searchid);
      if(mysql_query(conn , &sql_strexe[0])){
        mysql_close(conn);
        DieWithError("サーチ失敗\n");
      }
      break;
		case 1:
      sphone = (char *)malloc(sizeof(pos));
      if(sphone == NULL){
        DieWithError("sphoneメモリが確保できませんでした\n");
      }
      strcpy(sphone,pos);
      pos = strtok(NULL,"/");
      searchphone = atoi(sphone);
      #ifdef DEBUG
      printf("searchphone :%d\n",searchphone);
      #endif
      snprintf(&sql_strexe[0],sizeof(sql_strexe)-1,"select name,id from データベーステーブル名 where phone = %d",searchphone);
      if(mysql_query(conn , &sql_strexe[0])){
        mysql_close(conn);
        DieWithError("サーチ失敗\n");
      }
    break;
	}

	resp = mysql_use_result(conn);
  while((row = mysql_fetch_row(resp)) != NULL){
    snprintf(&sendName[0],sizeof(sendName)-1,"%s",row[0]);
    snprintf(&sendId[0],sizeof(sendId)-1,"%s",row[1]);
  }
  #ifdef DEBUG
  printf("検索結果NAME:%s\n",sendName);
  printf("検索結果ID:%s\n",sendId);
  #endif
	strcat(sendName,"/");
  searchinfo = sendName;
  strcat(searchinfo,sendId);
	searchinfolen = strlen(searchinfo);

	/*mysql接続終了*/
  mysql_free_result(resp);
  mysql_close(conn);

	/*検索ユーザー情報をクライアントに返信*/
  sendto(sock,searchinfo,searchinfolen,0,(struct sockaddr *)&echoClntAddr,sizeof(echoClntAddr));
}

/*ユーザ削除処理-------------------------------------------------------*/
void Delete(){
	int phone = 0;
  char *id;
  int ID = 0;
  int i = 1;
	char *c = "0"; //1:消去成功 0:データがない

	pos = strtok(echoBuffer,"/");
  pos = strtok(NULL,"/");
	while(pos != NULL){
		switch(i){
			case 1: //電話番号
        if((sphone = (char *)malloc(sizeof(pos))) == NULL){
          DieWithError("sphoneメモリが確保できませんでした\n");
        }
        strcpy(sphone,pos);
        phone = atoi(sphone);break;
      case 2: //ID
        if((id = (char *)malloc(sizeof(pos))) == NULL){
          DieWithError("idメモリが確保できませんでした\n");
        }
        strcpy(id,pos);
        ID = atoi(id);break;
      default:
        break;
		}
		pos = strtok(NULL,"/");
    i++;
	}

	DatabaseOpen();

	snprintf(&sql_strexe[0],sizeof(sql_strexe)-1,"DELETE FROM データベーステーブル名 WHERE phone = %d AND id = %d",phone,ID);
  free(sphone);free(id);
  if(mysql_query(conn,&sql_strexe[0])){
  }else{
    c = "1"; //成功
  }
  /*mysql接続終了*/
  mysql_close(conn);

  int clen = strlen(c);
	/*クライアントに結果を返す(1:成功 0:失敗)*/
	sendto(sock,c,clen,0,(struct sockaddr *)&echoClntAddr,sizeof(echoClntAddr));
  #ifdef DEBUG
  printf("結果(成功:1 失敗:0) :%s\n",c);
  #endif
}

/*ユーザ情報変更処理---------------------------------------------------*/
void Update(){
	int phone = 0;
  int i = 1;
  char *id;
  int ID = 0;
  char *c = "0"; //1:成功 0:失敗

	pos = strtok(echoBuffer,"/");
  pos = strtok(NULL,"/"); //mode(5/を取り除く)

	while(pos != NULL){
    switch(i){
      case 1: //名前
        if((name = (char *)malloc(sizeof(pos))) == NULL ){
          DieWithError("nameメモリが確保できませんでした\n");
        }
        strcpy(name,pos);
        break;
      case 2: //電話番号
        if((sphone = (char *)malloc(sizeof(pos))) == NULL){
          DieWithError("sphoneメモリが確保できませんでした\n");
        }
        strcpy(sphone,pos);
        phone = atoi(sphone);break;
        break;
      case 3:
        if((id = (char *)malloc(sizeof(pos))) == NULL ){
          DieWithError("idメモリが確保できませんでした\n");
        }
        strcpy(id,pos);
        ID = atoi(id);
        break;
      default:
        break;
    }
		pos = strtok(NULL,"/");
    i++;
	}
	#ifdef DEBUG
  printf("変更後: ユーザ名:%s 電話番号:%d ID:%d\n",name,phone,ID);
  #endif
  DatabaseOpen();

	snprintf(&sql_strexe[0],sizeof(sql_strexe)-1,"UPDATE データベーステーブル名 SET name = '%s',phone = %d where id = %d",name,phone,ID);
  free(name);free(sphone);free(id);

	if(mysql_query(conn,&sql_strexe[0])){
    DieWithError("ユーザー変更要求に失敗\n");
  }else{
    c = "1";
  }
	/*mysql接続終了*/
  mysql_close(conn);

  int clen = strlen(c);
	/*クライアントに結果を返す(1:成功 0:失敗)*/
	sendto(sock,c,clen,0,(struct sockaddr *)&echoClntAddr,sizeof(echoClntAddr));
  #ifdef DEBUG
  printf("結果(成功:1 失敗:0) :%s\n",c);
  #endif

}

/*受信処理-------------------------------------------------------------*/
void Reception(){
	memset(echoBuffer,'\0',recvMsgSize); //初期化
  /*クライアントからメッセージを受信するまでブロック*/
  if((recvMsgSize = recvfrom(sock,echoBuffer,ECHOMAX,0,(struct sockaddr *)&echoClntAddr,&cliAddrLen)) < 0){
    /*recvfrom()がブロックしようとした場合*/
    if(errno != EWOULDBLOCK){
      DieWithError("recvfrom()失敗\n");
    }
  }
}

/*データベースオープン-------------------------------------------------*/
void DatabaseOpen(){
	conn = mysql_init(NULL);
	if( !mysql_real_connect(conn,sql_serv,user,passwd,db_name,0,NULL,0)){
    DieWithError("データベース接続失敗\n");
  }
}

/*サーバが暇な時-------------------------------------------------------*/
void UseIdleTime(){
	char date[64];
	time_t t = time(NULL);
	strftime(date, sizeof(date), "%Y/%m/%d %a %H:%M:%S", localtime(&t));
	printf("\n%s : ", date);
	sleep(600); //10分置きに表示
}

/*標準エラー関数-------------------------------------------------------*/
void DieWithError(char *errorMessage){
	perror(errorMessage);
	exit(-1);
}
