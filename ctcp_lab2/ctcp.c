/******************************************************************************
 * ctcp.c
 * ------
 * Implementation of cTCP done here. This is the only file you need to change.
 * Look at the following files for references and useful functions:
 *   - ctcp.h: Headers for this file.
 *   - ctcp_iinked_list.h: Linked list functions for managing a linked list.
 *   - ctcp_sys.h: Connection-related structs and functions, cTCP segment
 *                 definition.
 *   - ctcp_utils.h: Checksum computation, getting the current time.
 *
 *****************************************************************************/

#include "ctcp.h"
#include "ctcp_linked_list.h"
#include "ctcp_sys.h"
#include "ctcp_utils.h"


#define TEST 1
#define TEST_DEBUG 0
/**
 * Packet data
 *
 */
typedef struct packet{
  ctcp_segment_t *segment;
  long last_time_send;
  uint8_t num_retransmit;
  bool ack_accept;
}packet_t;

/**
* Unacknowledged segment;
* 
*/
typedef struct unack_segment{
  ctcp_segment_t *segment;
  long last_time_send;
  uint8_t num_retransmit;
}unack_segment_t;

/**
 * Connection state.
 *
 * Stores per-connection information such as the current sequence number,
 * unacknowledged packets, etc.
 *
 * You should add to this to store other fields you might need.
 */
struct ctcp_state {
  struct ctcp_state *next;  /* Next in linked list */
  struct ctcp_state **prev; /* Prev in linked list */

  conn_t *conn;             /* Connection object -- needed in order to figure
                               out destination when sending */
  linked_list_t *segments;  /* Linked list of segments sent to this connection.
                               It may be useful to have multiple linked lists
                               for unacknowledged segments, segments that
                               haven't been sent, etc. Lab 1 uses the
                               stop-and-wait protocol and therefore does not
                               necessarily need a linked list. You may remove
                               this if this is the case for you */
  ctcp_config_t *config;
  bool check_read_EOF;
  bool check_receive_FIN;

  uint32_t last_byte_read;
  uint32_t last_byte_ack;
  uint32_t last_byte_output;

  linked_list_t *send_list;
  linked_list_t *recv_list;                          

  /* FIXME: Add other needed fields. */
};


typedef struct ctcp_state_send{
  uint32_t send_base;
  uint32_t current_send;
  uint32_t nextseqnum;
}ctcp_state_send_t;

typedef struct ctcp_state_receive{
  uint32_t recv_base;
  uint32_t first_seq_recv;
  uint32_t last_seqnum;
}ctcp_state_receive_t;

/**
 * Linked list of connection states. Go through this in ctcp_timer() to
 * resubmit segments and tear down connections.
 */
static ctcp_state_t *state_list;

ctcp_state_send_t *state_send;
ctcp_state_receive_t *state_receive;
linked_list_t *linked_list_unack_segment;

// int current_index_send = 0;

/* FIXME: Feel free to add as many helper functions as needed. Don't repeat
          code! Helper functions make the code clearer and cleaner. */
/*
  Funtion
  Segment in network byte order ntohl
*/
void segment_ntoh(ctcp_segment_t *segment)
{
  segment->seqno = ntohl(segment->seqno);
  segment->ackno = ntohl(segment->ackno);
  segment->len = ntohs(segment->len);
  segment->window = ntohs(segment->window);
  segment->flags = ntohl(segment->flags);
  //segment->cksum = ntohs(segment->cksum);


}

void segment_hton(ctcp_segment_t *segment)
{
  segment->seqno = htonl(segment->seqno);
  segment->ackno = htonl(segment->ackno);
  segment->len = htons(segment->len);
  segment->window = htons(segment->window);
  segment->flags = htonl(segment->flags);
  //segment->cksum = htons(segment->cksum);

}

void ctcp_send_sliding_window(ctcp_state_t *state);
ctcp_segment_t *generate_data_segment(ctcp_state_t *state, ctcp_segment_t *segment);
void ctcp_send_segment(ctcp_state_t *state,packet_t *packet);
void ctcp_send_ACK(ctcp_state_t* state,packet_t* packet);
void add_list_unacksegment(packet_t *packet);
int32_t check_continuous_in_recvlist(linked_list_t *list,packet_t *packet);
void add_packet_in_order(linked_list_t *list, packet_t *packet);


ctcp_state_t *ctcp_init(conn_t *conn, ctcp_config_t *cfg) {
  /* Connection could not be established. */
  if (conn == NULL) {
    return NULL;
  }

  /* Established a connection. Create a new state and update the linked list
     of connection states. */
  ctcp_state_t *state = calloc(sizeof(ctcp_state_t), 1);
  state->next = state_list;
  state->prev = &state_list;
  if (state_list)
    state_list->prev = &state->next;
  state_list = state;

  /* Set fields. */
  state->conn = conn;

  //state->last_byte_read = 0;
  state->last_byte_output = 0;
  state->last_byte_ack = 0;

  state->config = cfg;
  state->last_byte_read = 1;

  state->send_list = ll_create();
  state->recv_list = ll_create();

  state_send = (ctcp_state_send_t*)calloc(sizeof(ctcp_state_send_t),1);
  state_receive = (ctcp_state_receive_t*)calloc(sizeof(ctcp_state_receive_t),1);

  state_send->send_base = 1;
  state_send->current_send = 0;
  state_receive->recv_base = 1;

  /* FIXME: Do any other initialization here. */

  return state;
}

void ctcp_destroy(ctcp_state_t *state) {
  /* Update linked list. */
  if (state->next)
    state->next->prev = state->prev;

  *state->prev = state->next;
  conn_remove(state->conn);

  /* FIXME: Do any other cleanup here. */

  free(state);
  end_client();
}

#if TEST
void ctcp_read(ctcp_state_t *state) {
  /* FIXME */
  char *buffer = NULL;
  buffer = (char*)calloc(MAX_SEG_DATA_SIZE,1);
  uint32_t last_byte_segment_read = 1;
  int bytes_read = 0;  
  if (buffer == NULL)
  {
    return;
  }
  packet_t *packet_data;
  while ((bytes_read = conn_input(state->conn,buffer,MAX_SEG_DATA_SIZE)) > 0 )
  {
    packet_data = (packet_t*)calloc(sizeof(packet_t),1);
    packet_data->segment = (ctcp_segment_t*)calloc(sizeof(ctcp_segment_t) + bytes_read,1);

    packet_data->num_retransmit = 0;
    packet_data->last_time_send = current_time();
    packet_data->segment->seqno = state->last_byte_read;
    packet_data->segment->len = sizeof(ctcp_segment_t) + bytes_read; 
    memcpy(packet_data->segment->data,buffer,bytes_read);
    state->last_byte_read += bytes_read;
    ll_add(state->send_list,packet_data);
  }
  if (bytes_read == -1)
  {
    // read EOF
    state->check_read_EOF = true;
    packet_data->num_retransmit = 0;
    packet_data->last_time_send = current_time();
    packet_data->segment->seqno  = last_byte_segment_read;
    packet_data->segment->len = sizeof(ctcp_segment_t);
    //last_byte_segment_read += 1;
    ll_add(state->send_list,packet_data);
  }
  ctcp_send_sliding_window(state);
}
#endif

#if TEST_DEBUG
void ctcp_read(ctcp_state_t *state) {
  /* FIXME */
  char *buffer = NULL;
  buffer = (char*)calloc(MAX_SEG_DATA_SIZE,1);
  //uint32_t last_byte_segment_read = state->last_byte_read;
  int bytes_read = 0;  
  if (buffer == NULL)
  {
    return;
  }
  packet_t *packet_data;
  int index = 0;
  for (index = 0;index < 3;index ++)
  {

    // bytes_read = conn_input(state->conn,buffer,MAX_SEG_DATA_SIZE);
    fgets(buffer, sizeof(buffer), stdin);
    // Loại bỏ ký tự newline cuối chuỗi (nếu có)
    buffer[strcspn(buffer, "\n")] = '\0';

    // Đếm số ký tự trong chuỗi
    bytes_read = strlen(buffer) + 1;

  
    packet_data = (packet_t*)calloc(sizeof(packet_t),1);
    packet_data->segment = (ctcp_segment_t*)calloc(sizeof(ctcp_segment_t) + bytes_read,1);

    packet_data->num_retransmit = 0;
    packet_data->last_time_send = current_time();
    packet_data->segment->seqno = state->last_byte_read;
    fprintf(stderr,"%d\n",state->last_byte_read);
    packet_data->segment->len = sizeof(ctcp_segment_t) + bytes_read; 
    memcpy(packet_data->segment->data,buffer,bytes_read);
    state->last_byte_read += bytes_read;
    ll_add(state->send_list,packet_data);
    //sleep(2);
  }
  //int index;
  //ll_node_t *node ;
  fprintf(stderr,"%d\n",ll_length(state->send_list));
  // if (bytes_read == -1)
  // {
  //   // read EOF
  //   state->check_read_EOF = true;
  //   packet_data->num_retransmit = 0;
  //   packet_data->last_time_send = current_time();
  //   packet_data->segment->seqno  = last_byte_segment_read;
  //   packet_data->segment->len = sizeof(ctcp_segment_t);
  //   packet_data->segment->flags |= FIN;
  //   //last_byte_segment_read += 1;
  //   ll_add(state->send_list,packet_data);
  // }
  ctcp_send_sliding_window(state);

}
#endif

void ctcp_send_sliding_window(ctcp_state_t *state)
{
  unsigned int index = 0;
  unsigned int len_of_sendlist = ll_length(state->send_list);
  uint32_t last_seqno_segment_send;
  uint32_t last_seqno_window;
  uint16_t data_len;
  // ctcp_segment_t *segment;
  packet_t *packet;
  ll_node_t *node;
  //ll_node_t *temp;

  if (len_of_sendlist == 0)
  {
    return;
  }
  
  node = ll_front(state->send_list);
  for (index = 0; index < state_send->current_send; index ++)
  {
    node = node->next;
  }

  for (index = state_send->current_send ; index < len_of_sendlist; index++)
  {
    packet = (packet_t*)node->object;

    data_len = packet->segment->len - sizeof(ctcp_segment_t);
    last_seqno_segment_send = packet->segment->seqno + data_len + 1;
    last_seqno_window = state->last_byte_output + state->config->recv_window;

    if (last_seqno_segment_send >= last_seqno_window)
    {
      return;
    }

    ctcp_send_segment(state,packet);
    sleep(2);

    //state->last_byte_read = packet->segment->seqno;
    state->last_byte_ack = 1;

    state_send->nextseqnum = packet->segment->seqno; 
    
    //temp = node;
    node = node->next;
    fprintf(stderr,"+%d\n",index);
    //ll_remove(state->send_list,temp);

  }
  state_send->current_send = index;
  
}

ctcp_segment_t *generate_data_segment(ctcp_state_t *state, ctcp_segment_t *segment)
{
  ctcp_segment_t * data_segment;
  uint16_t data_len = segment->len - sizeof(ctcp_segment_t);
  uint16_t len_segment = sizeof(ctcp_segment_t) + data_len;
  data_segment = (ctcp_segment_t*)calloc(len_segment, 1);

  data_segment->seqno = segment->seqno;
  data_segment->ackno = segment->ackno;
  data_segment->len = len_segment;
  data_segment->flags |= ACK;
  data_segment->window = state->config->recv_window;
  memcpy(data_segment->data,segment->data,data_len);
  segment_hton(data_segment);
  data_segment->cksum = 0;
  data_segment->cksum = cksum(data_segment,len_segment);

  return data_segment;
}

void ctcp_send_segment(ctcp_state_t *state,packet_t *packet)
{
  ctcp_segment_t * data_segment = generate_data_segment(state,packet->segment);
  conn_send(state->conn,data_segment,ntohs(data_segment->len));

}

void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
  uint16_t data_len;
  packet_t *packet_recv;
  uint16_t checksum_check;
  uint16_t checksum_recv;
  uint32_t seqno;
  ll_node_t *temp;
  packet_t *packet;

  //segment_ntoh(segment);
  if (len < ntohs(segment->len))
  {
    free(segment);
    return;
  }

  checksum_recv = segment->cksum;
  segment->cksum = 0;
  checksum_check = cksum(segment,ntohs(segment->len));
  if (checksum_recv != checksum_check)
  {
    free(segment);
    return;
  }

  segment_ntoh(segment);
  ll_node_t *node = ll_front(state->recv_list);
  unsigned int len_of_recvlist = ll_length(state->recv_list);

  data_len = len - sizeof(ctcp_segment_t);
  if (segment != NULL)
  {
    fprintf(stderr,"segment\n");
    data_len = len - sizeof(ctcp_segment_t);
    packet_recv = (packet_t*)calloc(sizeof(packet_t),1);
    packet_recv->segment = (ctcp_segment_t*)calloc(sizeof(ctcp_segment_t) + data_len,1);

    packet_recv->segment->seqno = segment->seqno;
    packet_recv->segment->len = segment->len; 
    memcpy(packet_recv->segment->data,segment->data,data_len);
    
    state->last_byte_ack += data_len;
    state_receive->last_seqnum += data_len;

    if (segment->seqno > state_receive->recv_base)
    {
      fprintf(stderr,"err\n");
      //ll_add(state->recv_list,packet_recv);
      add_packet_in_order(state->recv_list,packet_recv);
    }
    else if (segment->seqno == state_receive->recv_base)
    {
      if (len_of_recvlist == 0)
      {
        data_len = segment->len - sizeof(ctcp_segment_t);
        conn_output(state->conn,segment->data,data_len);
        fprintf(stderr," %d : %d\n",segment->seqno,state_receive->recv_base);

        state_receive->recv_base += data_len;
        fprintf(stderr,"1\n");

        temp = node;
        //node = node->next;
        //ll_remove(state->recv_list,temp);
      }
      else
      {
        fprintf(stderr,"2\n");
        packet = (packet_t*)node->object;
        seqno = check_continuous_in_recvlist(state->recv_list,packet);
        if (seqno == -1)
        {
          fprintf(stderr,"seq = -1\n");
          return;
        }
        while(packet_recv->segment->seqno <= seqno)
        {
          data_len = packet_recv->segment->len - sizeof(ctcp_segment_t);
          conn_output(state->conn,packet_recv->segment->data,data_len);
          state_receive->recv_base += data_len;

          temp = node;
          node = node->next;
          ll_remove(state->recv_list,temp); 
        }
      }
    }
    else if (segment->seqno < state_receive->recv_base)
    {
      fprintf(stderr," %d < %d\n",segment->seqno,state_receive->recv_base);
    }

    //ctcp_output(state);

    //conn_output(state->conn,segment->data,data_len);
    //free(segment);
  }
  free(segment);
}

void ctcp_output(ctcp_state_t *state) {
  /* FIXME */
  // In order bytes, should conn_output()
  //int index = 0;
  uint16_t data_len;
  uint32_t seqno;
  ll_node_t *temp;
  unsigned int len_of_recvlist = ll_length(state->recv_list);
  ll_node_t *node = ll_front(state->recv_list);
  packet_t *packet;
  do{
    packet = (packet_t*)node->object;
    if (packet->segment->seqno > state_receive->recv_base )
    {
      fprintf(stderr,"11\n");
      return;
    }
    else
    {
      if (len_of_recvlist == 1)
      {
        data_len = packet->segment->len - sizeof(ctcp_segment_t);
        conn_output(state->conn,packet->segment->data,data_len);
        state_receive->recv_base += data_len;
        packet->segment->ackno = state_receive->recv_base;
        fprintf(stderr,"1\n");

        ctcp_send_ACK(state,packet);
        temp = node;
        //node = node->next;
        ll_remove(state->recv_list,temp);
      }
      else
      {
        fprintf(stderr,"2\n");

        seqno = check_continuous_in_recvlist(state->recv_list,packet);
        if (seqno == -1)
        {
          fprintf(stderr,"seq = -1\n");
          return;
        }
        while(packet->segment->seqno <= seqno)
        {
          data_len = packet->segment->len - sizeof(ctcp_segment_t);
          conn_output(state->conn,packet->segment->data,data_len);
          state_receive->recv_base += data_len;

          temp = node;
          node = node->next;
          ll_remove(state->recv_list,temp); 
        }

      }
      
    }
  }while(packet->segment->seqno <= seqno);

  
}

void ctcp_timer() {
  /* FIXME */

}


void ctcp_send_ACK(ctcp_state_t* state,packet_t* packet)
{
  ctcp_segment_t * segment;
  uint16_t len_segment = sizeof(ctcp_segment_t);
  segment = (ctcp_segment_t*)calloc(len_segment, 1);

  segment->seqno = packet->segment->seqno;
  segment->ackno = packet->segment->ackno;
  segment->len = len_segment;
  segment->flags |= ACK;
  segment->window = state->config->recv_window;
  segment_ntoh(segment);
  segment->cksum = 0;
  segment->cksum = cksum(segment,len_segment);

  conn_send(state->conn,segment,len_segment);
}

int32_t check_continuous_in_recvlist(linked_list_t *list,packet_t *packet)
{
  uint16_t data_len;
  ll_node_t *node;
  ll_node_t *node_next;
  packet_t *packet_data;
  packet_t *packet_data_next;

  int32_t ret_seqno;
  //bool check_continuous = true;

  node = ll_find(list,packet);
  if (node == NULL)
  {
    return -1;
  }
  packet_data = (packet_t*)node->object;
  fprintf(stderr,"seqno %d %d\n",packet_data->segment->seqno, packet_data->segment->len);

  do
  {
    data_len = packet_data->segment->len - sizeof(ctcp_segment_t);
    node_next = node->next;
    if (node_next == NULL)
    {
      //fprintf(stderr,"2\n");
      return -1;
    }

    packet_data_next = (packet_t*)node_next->object;
    if (packet_data->segment->seqno + data_len == packet_data_next->segment->seqno)
    {
      ret_seqno = packet_data->segment->seqno + data_len;
    }

    node = node->next;
  }while(node);

  return ret_seqno;
}
void add_list_unacksegment(linked_list_t *list,packet_t *packet)
{

}

void add_packet_in_order(linked_list_t *list, packet_t *packet)
{
  packet_t *packet_temp;
  uint16_t datalen_of_segment_temp;
  ll_node_t *node = ll_front(list);
  ctcp_segment_t *segment = packet->segment;
  while (node)
  {
    packet_temp = (packet_t*)node->object;
    ctcp_segment_t *segment_temp = packet_temp->segment;  
    datalen_of_segment_temp = segment_temp->len - sizeof(ctcp_segment_t);
    // List had packet 
    if (ll_find(list,packet))
    {
      return;
    }
    if (segment->seqno < segment_temp->seqno)
    {
      ll_add_front(list,packet);
      return;
    }
    if (segment->seqno >= segment_temp->seqno + datalen_of_segment_temp)
    {
      ll_add_after(list,node,packet);
      return;
    }
    node = node->next;
  }
}

