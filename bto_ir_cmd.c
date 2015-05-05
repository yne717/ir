#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <libusb-1.0/libusb.h>

#define VENDOR_ID  0x22ea
#define PRODUCT_ID 0x0039
#define BTO_EP_IN  0x81
#define BTO_EP_OUT 0x01
#define IR_FREQ 38000

#define INTERFACE_NUMBER 0

#define MAX_SIZE 64
#define IR_DATA_SIZE     7
#define IR_DATA_SIZE_EX 35

#define BTO_CMD_ECHO_BACK             0x41
#define BTO_CMD_GET_DATA              0x50
#define BTO_CMD_SET_RECEIVE_MODE      0x51
#define BTO_CMD_GET_DATA_EX           0x52
#define BTO_CMD_SET_RECEIVE_MODE_EX   0x53
#define BTO_CMD_GET_FIRMWARE_VERSION  0x56
#define BTO_CMD_SET_DATA              0x60
#define BTO_CMD_SET_DATA_EX           0x61

#define RECEIVE_WAIT_MODE_NONE  0
#define RECEIVE_WAIT_MODE_WAIT  1

#define ECHO_BACK    0
#define GET_DATA     1
#define RECEIVE_MODE 2
#define GET_VERSION  3
#define SET_DATA     4

#define IR_SEND_DATA_USB_SEND_MAX_LEN 14

static char bto_commands[] = {
  BTO_CMD_ECHO_BACK,
  BTO_CMD_GET_DATA,
  BTO_CMD_SET_RECEIVE_MODE,
  BTO_CMD_GET_FIRMWARE_VERSION,
  BTO_CMD_SET_DATA
};
static char bto_commands_ex[] = {
  BTO_CMD_ECHO_BACK,
  BTO_CMD_GET_DATA_EX,
  BTO_CMD_SET_RECEIVE_MODE_EX,
  BTO_CMD_GET_FIRMWARE_VERSION,
  BTO_CMD_SET_DATA_EX
};

char get_command(int no, int extend) {
  char cmd;
  if (extend)
    cmd = bto_commands_ex[no];
  else
    cmd = bto_commands[no];
  return cmd;
}

int get_data_length(int extend) {
  if (extend)
    return IR_DATA_SIZE_EX;
  else
    return IR_DATA_SIZE;
}

void close_device(libusb_context *ctx, libusb_device_handle *devh) {
  libusb_close(devh);
  libusb_exit(ctx);
}

libusb_device_handle* open_device(libusb_context *ctx) {
  struct libusb_device_handle *devh = NULL;
  libusb_device *dev;
  libusb_device **devs;

  int r = 1;
  int i = 0;
  int cnt = 0;

  libusb_set_debug(ctx, 3);
  
  if ((libusb_get_device_list(ctx, &devs)) < 0) {
    perror("no usb device found");
    exit(1);
  }

  /* check every usb devices */
  while ((dev = devs[i++]) != NULL) {
    struct libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(dev, &desc) < 0) {
      perror("failed to get device descriptor\n");
    }
    /* count how many device connected */
    if (desc.idVendor == VENDOR_ID && desc.idProduct == PRODUCT_ID) {
      cnt++;
    }
  }

  /* device not found */
  if (cnt == 0) {
    fprintf(stderr, "device not connected\n");
    exit(1);
  }

  if (cnt > 1) {
    fprintf(stderr, "multi device is not implemented yet\n");
    exit(1);
  }

  /* open device */
  if ((devh = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID)) < 0) {
    perror("can't find device\n");
    close_device(ctx, devh);
    exit(1);
  } 

  /* detach kernel driver if attached. */
  r = libusb_kernel_driver_active(devh, INTERFACE_NUMBER);
  if (r == 1) {
    /* detaching kernel driver */
    r = libusb_detach_kernel_driver(devh, INTERFACE_NUMBER);
    if (r != 0) {
      perror("detaching kernel driver failed");
      exit(1);
    }
  }

  r = libusb_claim_interface(devh, INTERFACE_NUMBER);
  if (r < 0) {
    fprintf(stderr, "claim interface failed (%d): %s\n", r, strerror(errno));
    exit(1);
  }

  return devh;
}

void write_device(struct libusb_device_handle *devh, unsigned char *cmd, int len) {
  int i, r;
  uint8_t buf[MAX_SIZE];

  int size = 0;

  memset(buf, 0xff, sizeof(buf));
  for (i = 0; i < len; i++) {
    buf[i] = cmd[i];
  }
  
  r = libusb_interrupt_transfer(devh, BTO_EP_OUT, buf, sizeof(buf) ,&size, 1000);
  if (r < 0) {
    fprintf(stderr, "libusb_interrupt_transfer (%d): %s\n", r, strerror(errno));
    exit(1);
  }
}

int read_device(struct libusb_device_handle *devh, unsigned char *buf, int bufsize) {
  int size = 0;
  memset(buf, 0x00, bufsize);

  int r = libusb_interrupt_transfer(devh, BTO_EP_IN, buf, bufsize, &size, 1000);
  if (r < 0) {
    fprintf(stderr, "libusb_interrupt_transfer (%d): %s\n", r, strerror(errno));
    exit(1);
  }

  return size;
}

void clear_device_buffer(struct libusb_device_handle *devh) {
  /* clear read buffer */
  unsigned char buf[MAX_SIZE];
  memset(buf, 0xff, sizeof(buf));
  buf[0] = BTO_CMD_ECHO_BACK;
  write_device(devh, buf, MAX_SIZE);
  read_device(devh, buf, MAX_SIZE);
}

int receive_ir(struct libusb_device_handle *devh, unsigned char *data, int length, int extend) {
  unsigned char buf[MAX_SIZE];
  char cmd;
  int i;
  int retval = 0;

  clear_device_buffer(devh);

  memset(buf, 0xff, sizeof(buf));
  cmd = get_command(RECEIVE_MODE, extend);
  buf[0] = cmd;
  buf[1] = RECEIVE_WAIT_MODE_WAIT;
  write_device(devh, buf, MAX_SIZE);
  read_device(devh, buf, MAX_SIZE);

  while (1) {
    memset(buf, 0xFF, sizeof(buf));
    cmd = get_command(GET_DATA, extend);
    buf[0] = cmd;
    write_device(devh, buf, MAX_SIZE);
    read_device(devh, buf, MAX_SIZE);
    if (buf[0] == cmd && buf[1] != 0) {
      for (i = 0; i < length; i++) {
        data[i] = buf[i+1];
      }
      retval = 1;
      break;
    }
  }

  memset(buf, 0xff, sizeof(buf));
  buf[0] = get_command(RECEIVE_MODE, extend);
  buf[1] = RECEIVE_WAIT_MODE_NONE;
  write_device(devh, buf, MAX_SIZE);
  read_device(devh, buf, MAX_SIZE);

  return retval;
}

void transfer_ir(struct libusb_device_handle *devh, char *data, int length, int extend) {
  unsigned char buf[MAX_SIZE];
  int i;
  char *e, s[3] = "\0\0";

  memset(buf, 0xff, MAX_SIZE);
  buf[0] = get_command(SET_DATA, extend);

  // hex to byte
  for (i = 0; i < length; i++) {
    s[0] = data[i*2];
    s[1] = data[i*2+1];
    buf[i+1] = (unsigned char)strtoul(s, &e, 16);
    if (*e != '\0') {
      break;
    }
  }

  if (i != length) {
    fprintf(stderr, "[%s] is skipped..\n", data);
    return;
  }

  write_device(devh, buf, MAX_SIZE);

  clear_device_buffer(devh);
}

void transfer_ir_codes(struct libusb_device_handle *devh, char *ir_data, int ir_data_size) {
  unsigned char buf[MAX_SIZE];
  int size;
  int send_bit_num = 0;
  int send_bit_pos = 0;
  int set_bit_size = 0;
  send_bit_num = ir_data_size / 4;

  while (1) {
    memset(buf, 0xff, MAX_SIZE);
    buf[0] = 0x34;
    buf[1] = (unsigned char)((send_bit_num >> 8) & 0xFF);
    buf[2] = (unsigned char)(send_bit_num        & 0xFF);
    buf[3] = (unsigned char)((send_bit_pos >> 8) & 0xFF);
    buf[4] = (unsigned char)(send_bit_pos        & 0xFF);

    if (send_bit_num > send_bit_pos) {
      set_bit_size = send_bit_num - send_bit_pos;
      if (set_bit_size > IR_SEND_DATA_USB_SEND_MAX_LEN) {
        set_bit_size = IR_SEND_DATA_USB_SEND_MAX_LEN;
      }
    } else {
      set_bit_size = 0;
    }
    buf[5] = (unsigned char)(set_bit_size & 0xFF);

    if (set_bit_size > 0) {
      unsigned int fi = 0;
      for (fi = 0; fi < set_bit_size; fi++) {
        buf[6 + (fi * 4)]     = ir_data[send_bit_pos  * 4];
        buf[6 + (fi * 4) + 1] = ir_data[(send_bit_pos * 4) + 1];
        buf[6 + (fi * 4) + 2] = ir_data[(send_bit_pos * 4) + 2];
        buf[6 + (fi * 4) + 3] = ir_data[(send_bit_pos * 4) + 3];
        send_bit_pos++;
      }

      int r = libusb_interrupt_transfer(devh, BTO_EP_OUT, buf, sizeof(buf) ,&size, 1000);
      if (r < 0) {
        fprintf(stderr, "libusb_interrupt_transfer (%d): %s\n", r, strerror(errno));
        exit(1);
      }
    } else {
      break;
    }
  }
  sleep(2);

  buf[0] = 0x35;
  buf[1] = (unsigned char)((IR_FREQ      >> 8) & 0xFF);
  buf[2] = (unsigned char)(IR_FREQ             & 0xFF);
  buf[3] = (unsigned char)((send_bit_num >> 8) & 0xFF);
  buf[4] = (unsigned char)(send_bit_num        & 0xFF);

  int r = libusb_interrupt_transfer(devh, BTO_EP_OUT, buf, sizeof(buf) ,&size, 1000);
  if (r < 0) {
    fprintf(stderr, "libusb_interrupt_transfer (%d): %s\n", r, strerror(errno));
    exit(1);
  }
}

int create_ir_code(unsigned int code_no, char *buff, int buff_size) {
  int code_size = 0;
  unsigned char code[9][4] = {
    { 0xD1, 0x2D, 0x08, 0xF7 },
    { 0xD1, 0x2D, 0x00, 0x00 },
    { 0xD1, 0x2D, 0x00, 0x00 },
    { 0xD1, 0x2D, 0x00, 0x00 },
    { 0xD1, 0x2D, 0x00, 0x00 },
    { 0xD1, 0x2D, 0x3C, 0xC3 },
    { 0xD1, 0x2D, 0x00, 0x00 },
    { 0xD1, 0x2D, 0x00, 0x00 },
    { 0xD1, 0x2D, 0x09, 0xF6 }
  };

  int c_need_data_len = 1224;
  unsigned char c_reader_code[] = { 0x01, 0x56, 0x00, 0xAB};
  unsigned char c_off_code[]    = { 0x00, 0x15, 0x00, 0x15};
  unsigned char c_on_code[]     = { 0x00, 0x15, 0x00, 0x40};
  unsigned char c_end_code[]    = { 0x00, 0x15, 0x08, 0xFD};
  int buff_set_pos = 0;

  code[1][2] = (unsigned char)(((code_no % 1000000) / 100000) + 0x30);
  code[1][3] = (unsigned char)(~code[1][2] & 0xFF);
  code[2][2] = (unsigned char)(((code_no % 100000)  /  10000) + 0x30);
  code[2][3] = (unsigned char)(~code[2][2] & 0xFF);
  code[3][2] = (unsigned char)(((code_no % 10000)   /   1000) + 0x30);
  code[3][3] = (unsigned char)(~code[3][2] & 0xFF);
  code[4][2] = (unsigned char)(((code_no % 1000)    /    100) + 0x30);
  code[4][3] = (unsigned char)(~code[4][2] & 0xFF);
  code[6][2] = (unsigned char)(((code_no % 100)     /     10) + 0x30);
  code[6][3] = (unsigned char)(~code[6][2] & 0xFF);
  code[7][2] = (unsigned char)((code_no % 10)                 + 0x30);
  code[7][3] = (unsigned char)(~code[7][2] & 0xFF);

  if (c_need_data_len <= buff_size) {
    int set_pos = 0;
    int set_data_count = 0;
    int fi, fj, fk, data_pos;
    for (fi = 0; fi < (sizeof(code) / sizeof(code[0])); fi++) {
      for (data_pos = 0; data_pos < sizeof(c_reader_code); data_pos++) {
        buff[buff_set_pos++] = c_reader_code[data_pos];
        set_data_count++;
      }
      for (fj = 0; fj < (sizeof(code[0]) / sizeof(code[0][0])); fj++) {
        for (fk = 0; fk < 8; fk++) {
          if (((code[fi][fj] >> fk) & 0x01) == 1) {
            for (data_pos = 0; data_pos < sizeof(c_on_code); data_pos++) {
              buff[buff_set_pos++] = c_on_code[data_pos];
              set_data_count++;
            }
          } else {
            for (data_pos = 0; data_pos < sizeof(c_off_code); data_pos++) {
              buff[buff_set_pos++] = c_off_code[data_pos];
              set_data_count++;
            }
          }
        }
      }
      for (data_pos = 0; data_pos < sizeof(c_end_code); data_pos++) {
        buff[buff_set_pos++] = c_end_code[data_pos];
        set_data_count++;
      }
    }
    if (set_data_count == c_need_data_len) {
      code_size = c_need_data_len;
    } else {
       code_size = 0;
    }
  } else {
    code_size = 0;
  }

  return code_size;
}

void usage() {
  fprintf(stderr, "usage: bto_ir_cmd <option>\n");
  fprintf(stderr, "  -k       \tKaraoke Infrared code.\n");
  fprintf(stderr, "  -r       \tReceive Infrared code.\n");
  fprintf(stderr, "  -t <code>\tTransfer Infrared code.\n");
  fprintf(stderr, "  -e       \tExtend Infrared code.\n");
  fprintf(stderr, "           \t  Normal:  7 octets\n");
  fprintf(stderr, "           \t  Extend: 35 octets\n");
}

int main(int argc, char *argv[]) {
  libusb_context *ctx = NULL;
  int r = 1;
  int i;
  unsigned char buf[MAX_SIZE];

  int receive_mode  = 0;
  int transfer_mode = 0;
  int karaoke_mode  = 0;
  int extend = 0;
  int ir_data_size;
  char *ir_data;

  while ((r = getopt(argc, argv, "kehrt:")) != -1) {
    switch(r) {
      case 'k':
        karaoke_mode = 1;
        break;
      case 'r':
        receive_mode = 1;
        fprintf(stderr, "Receive mode not support... m(_ _)m\n");
        exit(1);
      case 't':
        transfer_mode = 1;
        ir_data = optarg;
        break;
      case 'e':
        extend = 1;
        fprintf(stderr, "Extend mode not support... m(_ _)m\n");
        exit(1);
      default:
        usage();
        exit(1);
    }
  }

  if (karaoke_mode == 1 || receive_mode == 1 || transfer_mode == 1) {
    /* libusb initialize*/
    if ((r = libusb_init(&ctx)) < 0) {
      perror("libusb_init\n");
      exit(1);
    } 

    /* open device */
    libusb_device_handle *devh = open_device(ctx);

    ir_data_size = get_data_length(extend);

    if (karaoke_mode == 1) {
      int karaoke_num = atoi(ir_data);
      unsigned char ir_codes[4096];
      memset(ir_codes, 0x00, sizeof(ir_codes));
      ir_data_size = create_ir_code(karaoke_num, ir_codes, sizeof(ir_codes));
      printf("Karaoke mode\n");
      printf("  Karaoke code: %d\n", karaoke_num);
      transfer_ir_codes(devh, ir_codes, ir_data_size);
    }
    else if (transfer_mode == 1) {
      printf("Transfer mode\n");
      printf("  Transfer code: %s\n", ir_data);
      transfer_ir(devh, ir_data, ir_data_size, extend);
    }

    /* close device */
    close_device(ctx, devh);
  } 
  else {
    usage();
    exit(1);
  }

  return 0;
}
