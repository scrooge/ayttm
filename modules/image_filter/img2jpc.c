/*
 * Img2JPC Plugin for Ayttm - codec for jpg2000 images.
 */

#include "intl.h"
#ifdef __MINGW32__
#define __IN_PLUGIN__ 1
#endif
#include "plugin_api.h"
#include "image_window.h"
#include "mem_util.h"
#include "debug.h"

#include <jasper/jasper.h>


/*******************************************************************************
 *                             Begin Module Code
 ******************************************************************************/
/*  Module defines */
#define plugin_info img2jpc_LTX_plugin_info
#define module_version img2jpc_LTX_module_version


static int plugin_init();
static int plugin_finish();


/*  Module Exports */
PLUGIN_INFO plugin_info = {
	PLUGIN_FILTER,
	"Img2JPC",
	"Codec for JPG2000 images",
	"$Revision: 1.1 $",
	"$Date: 2003/12/17 11:58:05 $",
	NULL,
	plugin_init,
	plugin_finish,
	NULL,
	NULL
};
/* End Module Exports */

unsigned int module_version() {return CORE_VERSION;}

static unsigned char * img_2_jpg(const unsigned char *in_img, long *size);
static unsigned char * img_2_jpc(const unsigned char *in_img, long *size);

static unsigned char *(*old_img_2_jpg)(const unsigned char *, long *) = NULL;
static unsigned char *(*old_img_2_jpc)(const unsigned char *, long *) = NULL;

static int plugin_init()
{
	old_img_2_jpg = image_2_jpg;
	image_2_jpg = img_2_jpg;

/*	old_img_2_jpc = image_2_jpc;
	image_2_jpc = img_2_jpc;*/

	return 0;
}

static int plugin_finish()
{
	image_2_jpg = old_img_2_jpg;

/*	image_2_jpc = old_img_2_jpc;*/

	return 0;
}

/*******************************************************************************
 *                             End Module Code
 ******************************************************************************/

static unsigned char * img_2_img(const unsigned char *in_img, long *size, const char *soutfmt)
{
	char * out_img = NULL;
	jas_stream_t *in, *out;
	jas_image_t *image;
	int infmt;
	static int outfmt;

	/*
	static int ctr;
	char fn[100];
	FILE *fp;
	snprintf(fn, sizeof(fn), "ayttm-img-%03d.jpc", ctr++);
	fp = fopen(fn, "wb");
	if(fp) {
		fwrite(in_img, 1, *size, fp);
		fclose(fp);
	}
	*/
	if(jas_init()) {
		eb_debug(DBG_MOD, "Could not init jasper\n");
		return ay_memdup(in_img, *size);
	}
	if(!outfmt)
		outfmt = jas_image_strtofmt(soutfmt);

	if(!(in = jas_stream_memopen((unsigned char *)in_img, *size))) {
		eb_debug(DBG_MOD, "Could not open jasper input stream\n");
		return ay_memdup(in_img, *size);
	}
	infmt = jas_image_getfmt(in);
	eb_debug(DBG_MOD, "Got input image format: %d %s\n", infmt, jas_image_fmttostr(infmt));
	if(infmt <= 0)
		return ay_memdup(in_img, *size);

	if(!strcmp(jas_image_fmttostr(infmt), soutfmt)) {
		/* image is already in correct format */
		jas_stream_close(in);
		return ay_memdup(in_img, *size);
	}

	if(!(image = jas_image_decode(in, infmt, NULL))) {
		eb_debug(DBG_MOD, "Could not decode image format\n");
		return ay_memdup(in_img, *size);
	}

	if(!(out = jas_stream_memopen(out_img, 0))) {
		eb_debug(DBG_MOD, "Could not open output stream\n");
		return ay_memdup(in_img, *size);
	}

	eb_debug(DBG_MOD, "Encoding to format: %d %s\n", outfmt, soutfmt);
	if((jas_image_encode(image, out, outfmt, NULL))) {
		eb_debug(DBG_MOD, "Could not encode image format\n");
		return ay_memdup(in_img, *size);
	}
	jas_stream_flush(out);

	*size = ((jas_stream_memobj_t *)out->obj_)->bufsize_;
	eb_debug(DBG_MOD, "Encoded size is: %ld\n", *size);
	jas_stream_close(in);
	out_img=ay_memdup(((jas_stream_memobj_t *)out->obj_)->buf_, *size);
	jas_stream_close(out);
	jas_image_destroy(image);

	return out_img;
}

static unsigned char * img_2_jpg(const unsigned char *in_img, long *size)
{
	return img_2_img(in_img, size, "jpg");
}

static unsigned char * img_2_jpc(const unsigned char *in_img, long *size)
{
	return img_2_img(in_img, size, "jpc");
}