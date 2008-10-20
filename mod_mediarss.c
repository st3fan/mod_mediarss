/*
 * (C) Copyright 2008, Stefan Arentz, Arentz Consulting.
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <httpd.h>
#include <http_core.h>
#include <http_log.h>
#include <http_protocol.h>
#include <http_request.h>
#include <http_config.h>

// TODO Copied this from mod_rewrite .. isn't there a simple shortcut for this in apr?

static char *lookup_header(request_rec *r, const char *name)
{
   const apr_array_header_t *hdrs_arr;
   const apr_table_entry_t *hdrs;
   int i;

   hdrs_arr = apr_table_elts(r->headers_in);
   hdrs = (const apr_table_entry_t *)hdrs_arr->elts;
   for (i = 0; i < hdrs_arr->nelts; ++i) {
      if (hdrs[i].key == NULL) {
         continue;
      }
      if (strcasecmp(hdrs[i].key, name) == 0) {
         return hdrs[i].val;
      }
   }
   return NULL;
}

static int mediarss_index_directory(request_rec* r)
{
   apr_status_t status;
   apr_dir_t* dir;
   apr_finfo_t dirent;

   if ((status = apr_dir_open(&dir, r->filename, r->pool)) != APR_SUCCESS) {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, status, r, "Can't open directory for index: %s", r->filename);
      return HTTP_FORBIDDEN;
   }

   /* Content header */

   char* url;
   url = ap_construct_url(r->pool, r->uri, r);

   ap_set_content_type(r, "text/xml; charset=utf-8");

   ap_rputs("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n\n", r);
   ap_rputs("<rss version=\"2.0\" xmlns:media=\"http://search.yahoo.com/mrss/\">\n", r);
   ap_rputs("<channel>\n", r);
   ap_rvputs(r, "  <title>Index of ", url, "</title>\n", NULL);
   ap_rvputs(r, "  <link>", url, "</link>\n", NULL);

   /* Collect information about the files in the directory */
   
   while (1)
   {
      status = apr_dir_read(&dirent, APR_FINFO_MIN | APR_FINFO_NAME, dir);
      if (APR_STATUS_IS_INCOMPLETE(status)) {
         continue; /* ignore un-stat()able files */
      } else if (status != APR_SUCCESS) {
         break;
      }
      
      /* We are only interested in regular files. TODO Deal with symlinks. */
      
      if (dirent.filetype == APR_REG)
      {
         request_rec* rr;
         
         rr = ap_sub_req_lookup_dirent(&dirent, r, AP_SUBREQ_NO_ARGS, NULL);
         if (rr != NULL)
         {
            //if (rr->finfo.filetype == APR_REG && rr->status == OK)
            {
               char size[16];
               snprintf(size, sizeof(size), "%d", dirent.size);
               
               char date[APR_RFC822_DATE_LEN];
               apr_rfc822_date(date, dirent.mtime);
                           
               ap_rputs("  <item>\n", r);
               ap_rvputs(r, "    <guid>", url, dirent.name, "</guid>\n", NULL);
               ap_rvputs(r, "    <title>", dirent.name, "</title>\n", NULL);
               ap_rvputs(r, "    <pubDate>", date, "</pubDate>\n", NULL);
               ap_rvputs(r, "    <enclosure url=\"", url, dirent.name, "\" length=\"", size, "\"\n", NULL);
               ap_rvputs(r, "      type=\"", rr->content_type, "\"/>\n", NULL);
               ap_rvputs(r, "    <media:content url=\"", url, dirent.name, "\" fileSize=\"", size, "\"\n", NULL);
               ap_rvputs(r, "      type=\"", rr->content_type, "\">\n", NULL);
               ap_rputs("    </media:content>\n", r);
               ap_rputs("  </item>\n", r);
            }
            
            ap_destroy_sub_req(rr);
         }
      }
   }

   /* Content footer */

   ap_rputs("</channel>\n", r);
   ap_rputs("</rss>\n", r);
   
   return OK;
}

static int mediarss_handler(request_rec* r)
{
   if (strcmp(r->handler, DIR_MAGIC_TYPE) != 0) {
      return DECLINED;
   }

   int allow_opts = ap_allow_options(r);

   r->allowed |= (AP_METHOD_BIT << M_GET);
   if (r->method_number != M_GET) {
      return DECLINED;
   }
   
   if (r->method_number != M_GET) {
      return DECLINED;
   }

   // TODO This is lame and should be done in a different way

   const char* user_agent = lookup_header(r, "User-Agent");
   if (user_agent != NULL && strstr(user_agent, "iTunes") == NULL) {
      const char* accept = lookup_header(r, "Accept");
      if (accept == NULL || strcmp(accept, "application/rss+xml") != 0) {
         return DECLINED;
      }
   }

   if (allow_opts & OPT_INDEXES) {
      return mediarss_index_directory(r);
   } else {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "Directory index forbidden by rule: %s", r->filename);
      return HTTP_FORBIDDEN;
   }
}

static void mediarss_hooks(apr_pool_t* pool)
{
   ap_hook_handler(mediarss_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

module AP_MODULE_DECLARE_DATA mediarss_module = {
   STANDARD20_MODULE_STUFF,
   NULL,
   NULL,
   NULL,
   NULL,
   NULL,
   mediarss_hooks
};
