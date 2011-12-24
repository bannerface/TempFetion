#include "internal.h"

#include "accountopt.h"
#include "blist.h"
#include "conversation.h"
#include "dnsquery.h"
#include "debug.h"
#include "notify.h"
#include "privacy.h"
#include "prpl.h"
#include "plugin.h"
#include "util.h"
#include "version.h"
#include "network.h"
#include "xmlnode.h"
#include "request.h"
#include "imgstore.h"
#include "sslconn.h"

#include "sipmsg.h"
#include "dnssrv.h"
#include "ntlm.h"

#include "sipmsg.h"
#include "f_portrait.h"

void
CheckPortrait(struct fetion_account_data *sip, const gchar * who,
	      const gchar * crc)
{
	PurpleBuddy *buddy = NULL;
	struct fetion_buddy *f_buddy;
	const gchar *old_crc = NULL;
	buddy = purple_find_buddy(sip->account, who);
	g_return_if_fail(buddy != NULL);

	old_crc = purple_buddy_icons_get_checksum_for_user(buddy);
	if (old_crc != NULL && (strcmp(old_crc, crc) == 0))
		return;

	f_buddy = g_hash_table_lookup(sip->buddies, who);
	g_return_if_fail(f_buddy != NULL);
	f_buddy->icon_crc = g_strdup(crc);
	GetPortrait(sip, f_buddy, NULL);

}

void DownLoadPortrait(gpointer data, gint source, const gchar * error_message)
{
	gchar buf[10240];
	gchar *content_len, *cur, *pos;
	gchar *temp = NULL;

	gchar *header;
	gint len, rcv_len;
	struct fetion_account_data *sip;
	struct fetion_buddy *who = data;
	sip = who->sip;

	g_return_if_fail(who != NULL);

	purple_debug_info("fetion:",
			  "DownLoadPortrait starting...\n");
	rcv_len = read(source, buf, 10240);

	purple_debug_info("fetion:",
			  "Received: %d...\n", rcv_len);

	if (rcv_len > 0) {
		cur = strstr(buf, "\r\n\r\n");

		if (cur != NULL) {
			header = g_strndup(buf, cur-buf);
			purple_debug_info("fetion:",
					  "Received headr: %s...\n", header);
			g_free(header);

			// If the stat is not 200
			if (strncmp(buf, "HTTP/1.1 200 OK\r\n", 17) != 0) {
				// If the stat is not 302, then assume no portrait is for him
				if (strncmp(buf, "HTTP/1.1 302 Found\r\n", 20)
				    != 0) {
					who->icon_buf = NULL;
					purple_input_remove(who->inpa);
					return;
				}
				//fixme
				temp =
				    get_token(buf, "Location: http://",
					      "\r\n");

				purple_debug_info("fetion:",
				  "new URL: %s...\n", temp);

				if (temp != NULL && strlen(temp) > 7)
				{
					purple_input_remove(who->inpa);
					GetPortrait(sip, who, temp);
					return;
				}
				else 
					who->icon_buf = NULL;
				purple_debug_info("fetion:",
						  "DownLoadPortrait ip[%s][%s]\n",
						  temp, who->name);
				purple_input_remove(who->inpa);
				return;
			}
			temp = get_token(buf, "Content-Length: ", "\r\n");
			if (temp == NULL)
			{
				purple_input_remove(who->inpa);
				return;
			}
			content_len = g_strdup(temp);
			purple_debug_info("fetion:",
					  "DownLoadPortrait Content-Length%s\n",
					  content_len);
			if (content_len != NULL)
				who->icon_size = atoi(content_len);
			purple_debug_info("fetion:",
					  "DownLoadPortrait Content-Length%d\n",
					  who->icon_size);
			who->icon_rcv_len = 0;
			who->icon_buf = g_malloc0(who->icon_size);
			cur += 4;
			len = rcv_len - (cur - buf);
			memcpy(who->icon_buf, cur, len);
			who->icon_rcv_len = len;
			purple_debug_info("fetion:",
					  "DownLoadPortrait Received Length: %d\n",
					  len);

			while (who->icon_rcv_len < who->icon_size){
				rcv_len = read(source, buf, 10240);
				if (rcv_len<=0)
					break;
				memcpy(who->icon_buf+who->icon_rcv_len, buf, rcv_len);
				who->icon_rcv_len += rcv_len;

				purple_debug_info("fetion:",
					  "DownLoadPortrait Received Length: %d\n",
					  rcv_len);
			}

//			purple_debug_info("fetion:",
//					  "DownLoadPortrait begin[%s]\n", buf);
		} else if (who->icon_buf != NULL) {

			pos = (who->icon_buf) + (who->icon_rcv_len);
			memcpy(pos, buf, rcv_len);
			who->icon_rcv_len += rcv_len;
			if((who->icon_rcv_len)>=(who->icon_size))
				purple_input_remove(who->inpa);
		}
		else
				purple_input_remove(who->inpa);

//		purple_debug_info("fetion:", "DownLoadPortrait%d\n", rcv_len);
	} else {
		purple_input_remove(who->inpa);

		if (who->icon_rcv_len == who->icon_size)
			purple_buddy_icons_set_for_user(sip->account, who->name,
							who->icon_buf,
							who->icon_size,
							who->icon_crc);
		g_free(who->host);

	}

}

void GetPortrait_cb(gpointer data, gint source, const gchar * error_message)
{
	struct fetion_buddy *who = data;
	struct fetion_account_data *sip = who->sip;
	gchar *head, *server_ip;
	const gchar *ssic;
	gint writed_len;
	if (who->host == NULL)
		server_ip = g_strdup(sip->PortraitServer);
	else{
		server_ip = g_strdup(who->host);
	}

	ssic = purple_url_encode(sip->ssic);

	if (who->host == NULL){
		head = g_strdup_printf("GET /%s?%sUri=%s"
					   "&Size=%s&c=%s HTTP/1.1\r\n"
					   "User-Agent: IIC2.0/PC 3.6.1900\r\n"
					   "Accept: image/pjpeg;image/jpeg;image/bmp;image/x-windows-bmp;image/png;image/gif\r\n"
					   "Host: %s\r\n\r\n",
					   sip->PortraitPrefix,
					   (who->host ? "transfer=1&" : ""), who->name,
					   "96", ssic, server_ip);
	}
	else {
		head = g_strdup_printf("GET %s HTTP/1.1\r\n"
					   "User-Agent: IIC2.0/PC 3.6.1900\r\n"
					   "Accept: image/pjpeg;image/jpeg;image/bmp;image/x-windows-bmp;image/png;image/gif\r\n"
					   "Host: %s\r\n\r\n",
					   who->portrait_url, server_ip);
	}
	purple_debug_info("fetion:", "GetPortrait_cb:%s\n", head);
	who->inpa =
	    purple_input_add(source, PURPLE_INPUT_READ,
			     (PurpleInputFunction) DownLoadPortrait, who);
	writed_len = write(source, head, strlen(head));
	//g_free(head);
	//g_free(ssic);
	g_free(server_ip);
}

void
GetPortrait(struct fetion_account_data *sip, struct fetion_buddy *buddy,
	    const gchar * fullURL)
{
	PurpleProxyConnectData *conn;
	gchar *server_ip;
	g_return_if_fail(buddy != NULL);
	buddy->sip = sip;
	if (fullURL != NULL) {
		gchar *tp = strchr(fullURL, '/');
		server_ip = g_strndup(fullURL, tp-fullURL);
		buddy->host = g_strdup(server_ip);
		buddy->portrait_url = g_strdup(tp);
	} else 
		server_ip = g_strdup(sip->PortraitServer);

	purple_debug_info("fetion:", "GetPortrait:buddy[%s]\n", buddy->name);
	if ((conn = purple_proxy_connect(sip->gc, sip->account, server_ip,
					 80, GetPortrait_cb, buddy)) == NULL) {
		purple_connection_error_reason(sip->gc,
					       PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
					       _("Couldn't create socket"));
	}

	g_free(server_ip);

}

void UploadPortrait_cb(gpointer data, gint source, const gchar * error_message)
{
	struct fetion_account_data *sip = data;
	gsize max_write;
	gssize written;

	max_write = purple_circ_buffer_get_max_read(sip->icon_buf);
	if (max_write == 0) {
		purple_input_remove(sip->icon_handler);
		sip->icon_handler = 0;
		return;
	}
	written = write(source, sip->icon_buf->outptr, max_write);
	purple_debug_info("fetion:", "UploadPortrait[%d][%d]", max_write,
			  written);
	if (written < 0 && errno == EAGAIN)
		written = 0;
	else if (written <= 0) {
		/*TODO: do we really want to disconnect on a failure to write? */
		purple_connection_error_reason(sip->gc,
					       PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
					       _("Could not write"));
		return;
	}

	purple_circ_buffer_mark_read(sip->icon_buf, written);

}

void UploadPortrait(gpointer data, gint source, const gchar * error_message)
{
	struct fetion_account_data *sip = data;
	PurpleStoredImage *img = sip->icon;
	gchar *head;
	gchar *buf;
	gint head_len;
	gint ret, lenth, writed;
	gconstpointer img_data = purple_imgstore_get_data(img);
	size_t size = purple_imgstore_get_size(img);

	head = g_strdup_printf("POST /%s/setportrait.aspx HTTP/1.1\r\n"
			       "User-Agent: IIC2.0/PC 3.3.0370\r\n"
			       "Content-Type: image/jpeg\r\n"
			       "Host: %s\r\n"
			       "Cookie: ssic=%s\r\n"
			       "Content-Length: %d\r\n\r\n",
			       sip->UploadPrefix,
			       sip->UploadServer, sip->ssic, size);
	purple_debug_info("fetion:", "UploadPortrait:head[%s][%d]\n", head,
			  size);
	head_len = strlen(head);
	buf = g_malloc(head_len + size);
	memcpy(buf, head, head_len);
	memcpy(buf + head_len, img_data, size);
	lenth = size + strlen(head);
	writed = 0;
	ret = write(source, buf, lenth);
	if (ret < 0 && errno == EAGAIN)
		ret = 0;
	g_return_if_fail(ret >= 0);
	if (ret < lenth) {
		purple_circ_buffer_append(sip->icon_buf, buf + ret,
					  lenth - ret);
		sip->icon_handler =
		    purple_input_add(source, PURPLE_INPUT_WRITE,
				     (PurpleInputFunction) UploadPortrait_cb,
				     sip);

	}
	g_free(head);
	sip->icon = NULL;
	purple_imgstore_unref(img);
}

void fetion_set_buddy_icon(PurpleConnection * gc, PurpleStoredImage * img)
{
	size_t size = purple_imgstore_get_size(img);
	PurpleProxyConnectData *conn;
	struct fetion_account_data *sip = gc->proto_data;

	g_return_if_fail(img != NULL);

	if (size > 0x32000)
		return;
	sip->icon = img;

	purple_debug_info("fetion:", "set_buddy_icon:len[%d]\n", size);
	if ((conn =
	     purple_proxy_connect(sip->gc, sip->account, sip->UploadServer, 80,
				  UploadPortrait, sip)) == NULL) {
		purple_connection_error_reason(sip->gc,
					       PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
					       _("Couldn't create socket"));
	}

	purple_imgstore_ref(img);

}
