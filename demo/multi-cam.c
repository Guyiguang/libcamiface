#include <stdio.h>
#ifdef _WIN32
#include <Windows.h>
#include <sys/timeb.h>
#else
#include <sys/time.h>
#endif
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "cam_iface.h"

double my_floattime() {
#ifdef _WIN32
#if _MSC_VER == 1310
  struct _timeb t;
  _ftime(&t);
  return (double)t.time + (double)t.millitm * (double)0.001;
#else
  struct _timeb t;
  if (_ftime_s(&t)==0) {
    return (double)t.time + (double)t.millitm * (double)0.001;
  }
  else {
    return 0.0;
  }
#endif
#else
  struct timeval t;
  if (gettimeofday(&t, (struct timezone *)NULL) == 0)
    return (double)t.tv_sec + t.tv_usec*0.000001;
  else
    return 0.0;
#endif
}

#define _check_error() {						\
    int _check_error_err;						\
    _check_error_err = cam_iface_have_error();				\
    if (_check_error_err != 0) {					\
      									\
      fprintf(stderr,"%s:%d %s\n", __FILE__,__LINE__,cam_iface_get_error_string()); \
      exit(1);								\
    }									\
  }									\

void save_pgm(const char* filename,unsigned char *pixels,int width,int height) {
  int i,j;
  FILE* fd;
  fd = fopen(filename,"w");
  fprintf(fd,"P5\n");
  fprintf(fd,"%d %d\n",width,height);
  fprintf(fd,"255\n");
  fwrite(pixels,1,width*height,fd);
  fprintf(fd,"\n");
  fclose(fd);
}

void show_usage(char * cmd) {
  printf("usage: %s [num_frames]\n",cmd);
  printf("  where num_frames can be a number or 'forever'\n");
  exit(1);
}

int main(int argc, char** argv) {
  CamContext **cc;
  unsigned char **pixels;

  int num_cameras=2;
  int num_buffers;

  double last_fps_print, now, t_diff;
  double fps;
  int n_frames;
  int have_frame;
  int buffer_size;
  int num_modes, num_props;
  const char** mode_strings;
  char mode_string[255];
  int i,mode_number,camno;
  CameraPropertyInfo cam_props;
  long prop_value;
  int prop_auto;
  int errnum;
  int width, height;
  int do_num_frames;
  CameraPixelCoding coding;

  char save_fname[100];

  cam_iface_startup_with_version_check();
  _check_error();

  if (argc>1) {
    if (strcmp(argv[1],"forever")==0) {
      do_num_frames = -1;
    } else if (sscanf(argv[1],"%d",&do_num_frames)==0) {
      show_usage(argv[0]);
    }
  } else {
    do_num_frames = 50;
  }

  for (i=0;i<argc;i++) {
    printf("%d: %s\n",i,argv[i]);
  }
  printf("using driver %s\n",cam_iface_get_driver_name());

  printf("%d cameras found\n",cam_iface_get_num_cameras());

  if (cam_iface_get_num_cameras()<num_cameras) {
    _check_error();

    printf("not enough cameras found, will now exit\n");

    cam_iface_shutdown();
    _check_error();

    exit(1);
  }
  _check_error();

  cc = (CamContext**)malloc( num_cameras*sizeof(CamContext**));
  if (cc==NULL) {
    printf("error allocating memory, will now exit\n");

    cam_iface_shutdown();
    _check_error();

    exit(1);
  }

  pixels = (unsigned char **)malloc( num_cameras*sizeof(unsigned char**));
  if (pixels==NULL) {
    printf("error allocating memory, will now exit\n");

    cam_iface_shutdown();
    _check_error();

    exit(1);
  }


  for (camno=0; camno<num_cameras; camno++) {
    printf("initializing camera number %d\n",camno);

    cam_iface_get_num_modes(camno, &num_modes);
    _check_error();

    printf("%d mode(s) available:\n",num_modes);

    mode_number = 10;

    for (i=0; i<num_modes; i++) {
      cam_iface_get_mode_string(camno,i,mode_string,255);
      printf("  %d: %s\n",i,mode_string);
    }
    printf("Choosing mode %d\n",mode_number);

    num_buffers = 5;

    cc[camno] = new_CamContext(camno,num_buffers,mode_number);
    _check_error();

    CamContext_get_frame_size(cc[camno], &width, &height);
    _check_error();

    CamContext_get_num_framebuffers(cc[camno],&num_buffers);
    printf("allocated %d buffers\n",num_buffers);

    CamContext_get_num_camera_properties(cc[camno],&num_props);
    _check_error();

    for (i=0; i<num_props; i++) {
      CamContext_get_camera_property_info(cc[camno],i,&cam_props);
      _check_error();

      if (strcmp(cam_props.name,"white balance")==0) {
	fprintf(stderr,"WARNING: ignoring white balance property\n");
	continue;
      }

      if (cam_props.is_present) {
	CamContext_get_camera_property(cc[camno],i,&prop_value,&prop_auto);
	_check_error();
	printf("  %s: %d\n",cam_props.name,prop_value);
      } else {
	printf("  %s: not present\n");
      }
    }

    CamContext_get_buffer_size(cc[camno],&buffer_size);
    _check_error();

    if (buffer_size == 0) {
      fprintf(stderr,"buffer size was 0 in %s, line %d\n",__FILE__,__LINE__);
      exit(1);
    }

    pixels[camno] = (unsigned char *)malloc( buffer_size );
    if (pixels[camno]==NULL) {
      fprintf(stderr,"couldn't allocate memory in %s, line %d\n",__FILE__,__LINE__);
      exit(1);
    }

    CamContext_start_camera(cc[camno]);
    _check_error();
  }

  last_fps_print = my_floattime();
  n_frames = 0;

  if (do_num_frames < 0) {
    printf("will now run forever. press Ctrl-C to interrupt\n");
  } else {
    printf("will now grab %d frames.\n",do_num_frames);
  }

  /*
    CamContext_set_trigger_mode_number( cc[camno], 0 );
    _check_error();
  */

  while (1) {
    if (do_num_frames<0) break;

    have_frame = 0;
    for (camno=0; camno<num_cameras; camno++) {
      CamContext_grab_next_frame_blocking(cc[camno],pixels[camno],0.001); // timeout after 1 msec
      errnum = cam_iface_have_error();

      if (errnum == CAM_IFACE_FRAME_TIMEOUT) {
	cam_iface_clear_error();
	continue; // wait again on next camera
      } else if (errnum == CAM_IFACE_FRAME_DATA_MISSING_ERROR) {
	cam_iface_clear_error();
	fprintf(stdout,"M");
	fflush(stdout);
	continue; // wait again on next camera
      }

      _check_error();
      have_frame = 1;

      fprintf(stdout,"%d",camno);
      fflush(stdout);
    }

    if (!have_frame) {
      continue;
    }

    do_num_frames--;
  }

  for (camno=0; camno<num_cameras; camno++) {
    coding = cc[camno]->coding;

    printf("\n");
    delete_CamContext(cc[camno]);
    _check_error();

    if (coding==CAM_IFACE_MONO8) {
      snprintf(save_fname, 100, "image_camera%d.pgm", camno);
      save_pgm(save_fname, pixels[camno], width, height);
      printf("saved last image as %s\n",save_fname);
    } else {
      printf("do not know how to save sample image for this format\n");
    }

    free(pixels[camno]);
  }

  free(pixels);

  cam_iface_shutdown();
  _check_error();

  return 0;
}
