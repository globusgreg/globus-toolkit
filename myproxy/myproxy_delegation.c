#include "myproxy_common.h"	/* all needed headers included here */

static int
auth_sasl_negotiate_client(myproxy_socket_attrs_t *attrs,
		       char *user_id);

static int copy_file(const char *source,
		     const char *dest,
		     const mode_t mode);

static int myproxy_authorize_init(myproxy_socket_attrs_t *attrs,
                                  char *passphrase,
				  char *certfile,
				  char *kerberos_username);

int myproxy_set_delegation_defaults(
    myproxy_socket_attrs_t *socket_attrs,
    myproxy_request_t      *client_request)
{ 
    char *username, *pshost;

    client_request->version = strdup(MYPROXY_VERSION);
    client_request->command_type = MYPROXY_GET_PROXY;

    username = getenv("LOGNAME");
    if (username != NULL) {
      client_request->username = strdup(username);
    }

    pshost = getenv("MYPROXY_SERVER");
    if (pshost != NULL) {
        socket_attrs->pshost = strdup(pshost);
    }

    client_request->proxy_lifetime = 60*60*MYPROXY_DEFAULT_DELEG_HOURS;

    socket_attrs->psport = MYPROXY_SERVER_PORT;

    return 0;
}
    
int myproxy_get_delegation(
    myproxy_socket_attrs_t *socket_attrs,
    myproxy_request_t      *client_request,
    char *certfile,
    char *kerberos_username,
    myproxy_response_t     *server_response,
    char *outfile)
{    
    char delegfile[128];
    char request_buffer[2048];
    int  requestlen;

    /* Set up client socket attributes */
    if (myproxy_init_client(socket_attrs) < 0) {
        fprintf(stderr, "Error: %s\n", verror_get_string());
        return(1);
    }
    
    /* Attempt anonymous-mode credential retrieval if we don't have a
       credential. */
    GSI_SOCKET_allow_anonymous(socket_attrs->gsi_socket, 1);

     /* Authenticate client to server */
    if (myproxy_authenticate_init(socket_attrs, NULL) < 0) {
        fprintf(stderr, "Error: %s: %s\n", 
		socket_attrs->pshost, verror_get_string());
        return(1);
    }

    /* Serialize client request object */
    requestlen = myproxy_serialize_request(client_request, 
                                           request_buffer, sizeof(request_buffer));
    if (requestlen < 0) {
        fprintf(stderr, "Error in myproxy_serialize_request():\n");
        return(1);
    }

    /* Send request to the myproxy-server */
    if (myproxy_send(socket_attrs, request_buffer, requestlen) < 0) {
        fprintf(stderr, "Error in myproxy_send_request(): %s\n", 
		verror_get_string());
        return(1);
    }

    /* Continue unless the response is not OK */
    if (myproxy_authorize_init(socket_attrs, client_request->passphrase,
	                       certfile, kerberos_username) < 0) {
	  fprintf(stderr, "%s\n",
	          verror_get_string());
	  return(1);
    }

    /* Accept delegated credentials from server */
    if (myproxy_accept_delegation(socket_attrs, delegfile, sizeof(delegfile), NULL) < 0) {
        fprintf(stderr, "Error in myproxy_accept_delegation(): %s\n", 
		verror_get_string());
	return(1);
    }      

    if (myproxy_recv_response(socket_attrs, server_response) < 0) {
       fprintf(stderr, "%s\n", verror_get_string());
       return(1);
    }

    /* move delegfile to outputfile if specified */
    if (outfile != NULL) {
        if (copy_file(delegfile, outfile, 0600) < 0) {
		fprintf(stderr, "Error creating file: %s\n",
		outfile);
		return(1);
	}
	unlink(delegfile);
	strcpy(delegfile, outfile);
    }

    return(0);
}

/*
 * copy_file()
 *
 * Copy source to destination, creating destination if necessary
 * Set permissions on destination to given mode.
 *
 * Returns 0 on success, -1 on error. 
 */
static int
copy_file(const char *source,
	  const char *dest,
	  const mode_t mode)
{
    int src_fd = -1;
    int dst_fd = -1;
    int src_flags = O_RDONLY;
    int dst_flags = O_WRONLY | O_CREAT;
    char buffer[2048];
    int bytes_read;
    int return_code = -1;
    
    assert(source != NULL);
    assert(dest != NULL);
    
    src_fd = open(source, src_flags);
    
    if (src_fd == -1)
    {
	verror_put_errno(errno);
	verror_put_string("opening %s for reading", source);
	goto error;
    }
     
    dst_fd = open(dest, dst_flags, mode);
    
    if (dst_fd == -1)
    {
	verror_put_errno(errno);
	verror_put_string("opening %s for writing", dest);
	goto error;
    }
    
    do 
    {
	bytes_read = read(src_fd, buffer, sizeof(buffer));
	
	if (bytes_read == -1)
	{
	    verror_put_errno(errno);
	    verror_put_string("reading %s", source);
	    goto error;
	}

	if (bytes_read != 0)
	{
	    if (write(dst_fd, buffer, bytes_read) == -1)
	    {
		verror_put_errno(errno);
		verror_put_string("writing %s", dest);
		goto error;
	    }
	}
    }
    while (bytes_read > 0);
    
    /* Success */
    return_code = 0;
	
  error:
    if (src_fd != -1)
    {
	close(src_fd);
    }
    
    if (dst_fd != -1)
    {
	close(dst_fd);

	if (return_code == -1)
	{
	    unlink(dest);
	}
    }
    
    return return_code;
}

static int
myproxy_authorize_init(myproxy_socket_attrs_t *attrs,
                       char *passphrase,
		       char *certfile,
		       char *kerberos_username)
{
   myproxy_response_t *server_response = NULL;
   myproxy_response_t *server_buf = NULL;
   myproxy_proto_response_type_t response_type;
   authorization_data_t *d; 
   /* just pointer into server_response->authorization_data, no memory is 
      allocated for this pointer */
   int return_status = -1;
   char buffer[8192];
   int bufferlen;
   
   do {
      server_response = malloc(sizeof(*server_response));
      memset(server_response, 0, sizeof(*server_response));
      if (myproxy_recv_response(attrs, server_response) < 0) {
	 goto end;
      }

      response_type = server_response->response_type;
      if (response_type == MYPROXY_AUTHORIZATION_RESPONSE) {
	 if (certfile != NULL)
	    d = authorization_create_response(
		               server_response->authorization_data,
			       AUTHORIZETYPE_CERT,
			       certfile,
			       strlen(certfile) + 1);
	 else if (kerberos_username != NULL) {
	    d = authorization_create_response(
		               server_response->authorization_data,
			       AUTHORIZETYPE_SASL,
			       kerberos_username,
			       strlen(kerberos_username) + 1);
	 }
	 else 
	    d = authorization_create_response(
		              server_response->authorization_data,
			      AUTHORIZETYPE_PASSWD,
			      passphrase,
			      strlen(passphrase) + 1);
	 if (d == NULL) {
	    verror_put_string("Cannot create authorization response");
       	    goto end;
	 }

	 if (d->client_data_len + sizeof(int) > sizeof(buffer)) {
	       verror_put_string("Internal buffer too small");
	       goto end;
	 }
	 (*buffer) = d->method;
	 bufferlen = d->client_data_len + sizeof(int);

	 memcpy(buffer + sizeof(int), d->client_data, d->client_data_len);
	 
	 /* Send the authorization data to the server */
	 if (myproxy_send(attrs, buffer, bufferlen) < 0) {
	    goto end;
	 }
	 
	 if (kerberos_username != NULL) {
//	 	printf("Client: begin SASL negotiation...\n");
		if (auth_sasl_negotiate_client(attrs, kerberos_username) < 0)
			goto end;
	 }

      }
      myproxy_free(NULL, NULL, server_response);
      server_response = NULL;
   } while (response_type == MYPROXY_AUTHORIZATION_RESPONSE);

   return_status = 0;
end:
   myproxy_free(NULL, NULL, server_response);
   myproxy_free(NULL, NULL, server_buf);

   return return_status;
}


int send_response_sasl_data(myproxy_socket_attrs_t *attrs,
    			myproxy_response_t* server_response,
                        const char *data, int data_len)
{
    char client_buffer[SASL_BUFFER_SIZE], buf[SASL_BUFFER_SIZE];
    int  bufferlen, len, result;

    authorization_data_t*  auth_data;
	
    sasl_encode64(data, data_len, buf, SASL_BUFFER_SIZE, &len);
    buf[len] = '\0';
    if (result != SASL_OK) {
       verror_put_string("Encoding data in base64 failed in send_response_sasl_data");
       return -1;
    }
//    printf("C: %s\n", buf);

    auth_data = authorization_create_response(
	               server_response->authorization_data,
		       AUTHORIZETYPE_SASL,
		       buf,
		       len + 1);

    if (auth_data == NULL) {
	verror_put_string("Cannot create authorization response in send_response_sasl_data");
	return -1;
    }

    if (auth_data->client_data_len + sizeof(int) > sizeof(client_buffer)) {
        verror_put_string("Internal buffer too small send_response_sasl_data");
        return -1;
    }
    
    (*client_buffer) = AUTHORIZETYPE_SASL;
    bufferlen = auth_data->client_data_len + sizeof(int);

    memcpy(client_buffer + sizeof(int), auth_data->client_data, auth_data->client_data_len);
	 
    if (myproxy_send(attrs, client_buffer, bufferlen) < 0) 
       return -1;
    return 0;
}


int recv_response_sasl_data(myproxy_socket_attrs_t *attrs,
                        myproxy_response_t* server_response,
                        char *data)
{
    char *response_data;
    int result;
    int len;
    authorization_data_t*  auth_data;
    
    if (myproxy_recv_response(attrs, server_response) < 0) 
        return -1;
	
    auth_data = authorization_create_response(
	               server_response->authorization_data,
		       AUTHORIZETYPE_SASL,
		       NULL,
		       0);
    
    response_data = auth_data->server_data;
//    printf("S: %s\n", response_data); 
    result = sasl_decode64(response_data, strlen(response_data), 
		    data, SASL_BUFFER_SIZE, &len);
    if (result != SASL_OK) {
        verror_put_string("Decoding data from base64 failed.\n");
        verror_put_errno(errno);
        return -1;
    }
    data[len] = '\0';
    return len;
}


static sasl_conn_t *conn = NULL;


static int
getpath(void *context,
        const char ** path)
{
  const char *searchpath = (const char *) context;

  if (! path)
    return SASL_BADPARAM;

  if (searchpath) {
      *path = searchpath;
  } else {
      *path = SASL_LIBRARY_PATH;
  }

  return SASL_OK;
}


static int
simple(void *context,
       int id,
       const char **result,
       unsigned *len)
{
  const char *value = (const char *)context;

  if (! result)
    return SASL_BADPARAM;

  switch (id) {
  case SASL_CB_USER:
    *result = value;
    if (len)
      *len = value ? strlen(value) : 0;
    break;
  case SASL_CB_AUTHNAME:
    *result = value;
    if (len)
      *len = value ? strlen(value) : 0;
    break;
  case SASL_CB_LANGUAGE:
    *result = NULL;
    if (len)
      *len = 0;
    break;
  default:
    return SASL_BADPARAM;
  }

//  printf("returning OK: %s\n", *result);

  return SASL_OK;
}


static char *
getpassphrase(const char *prompt)
{
  return getpass(prompt);
}


static int
getsecret(sasl_conn_t *conn,
          void *context __attribute__((unused)),
          int id,
          sasl_secret_t **psecret)
{
  char *password;
  size_t len;

  if (! conn || ! psecret || id != SASL_CB_PASS)
    return SASL_BADPARAM;

  password = getpassphrase("Password: ");
  if (! password)
    return SASL_FAIL;

  len = strlen(password);

  *psecret = (sasl_secret_t *) malloc(sizeof(sasl_secret_t) + len);

  if (! *psecret) {
    memset(password, 0, len);
    return SASL_NOMEM;
  }

  (*psecret)->len = len;
  strcpy((char *)(*psecret)->data, password);
  memset(password, 0, len);

  return SASL_OK;
}


static int
prompt(void *context __attribute__((unused)),
       int id,
       const char *challenge,
       const char *prompt,
       const char *defresult,
       const char **result,
       unsigned *len)
{
  if ((id != SASL_CB_ECHOPROMPT && id != SASL_CB_NOECHOPROMPT)
      || !prompt || !result || !len)
    return SASL_BADPARAM;

  if (! defresult)
    defresult = "";

  fputs(prompt, stdout);
  if (challenge)
    printf(" [challenge: %s]", challenge);
  printf(" [%s]: ", defresult);
  fflush(stdout);

  if (id == SASL_CB_ECHOPROMPT) {
    char *original = getpassphrase("");
    if (! original)
      return SASL_FAIL;
    if (*original)
      *result = strdup(original);
    else
      *result = strdup(defresult);
    memset(original, 0L, strlen(original));
  } else {
    char buf[1024];
    fgets(buf, 1024, stdin);
    if (buf[0]) {
      *result = strdup(buf);
    } else {
      *result = strdup(defresult);
    }
    memset(buf, 0L, sizeof(buf));
  }
  if (! *result)
    return SASL_NOMEM;

  *len = strlen(*result);

  return SASL_OK;
}



static int
auth_sasl_negotiate_client(myproxy_socket_attrs_t *attrs,
		       char *user_id)
{
    char server_buffer[SASL_BUFFER_SIZE];
    const char *data;
    int  len, server_len;

    myproxy_response_t server_response;

#define N_CALLBACKS (16)

    int result;
    sasl_security_properties_t secprops;
    sasl_callback_t callbacks[N_CALLBACKS], *callback;
    const char *chosenmech;
    char *mech = "GSSAPI",
        *iplocal = NULL,
        *ipremote = NULL,
        *searchpath = SASL_LIBRARY_PATH,
        *service = "host";
    char fqdn[1024];

    strcpy(fqdn, attrs->pshost);
   
    memset(server_buffer, 0, sizeof(*server_buffer));

    /* Init defaults... */
    memset(&secprops, 0L, sizeof(secprops));
    secprops.maxbufsize = SASL_BUFFER_SIZE;
    secprops.max_ssf = UINT_MAX;

    /* Fill in the callbacks that we're providing... */
    callback = callbacks;

    /* getpath */
    if (searchpath) {
        callback->id = SASL_CB_GETPATH;
        callback->proc = &getpath;
        callback->context = searchpath;
        ++callback;
    }

    /* user */
    if (user_id) {
//	printf("User (authorization) id to request: %s\n", user_id);
        callback->id = SASL_CB_USER;
        callback->proc = &simple;
        callback->context = user_id;
        ++callback;
    }

    /* password */
    callback->id = SASL_CB_PASS;
    callback->proc = &getsecret;
    callback->context = NULL;
    ++callback;

    /* echoprompt */
    callback->id = SASL_CB_ECHOPROMPT;
    callback->proc = &prompt;
    callback->context = NULL;
    ++callback;

    /* noechoprompt */
    callback->id = SASL_CB_NOECHOPROMPT;
    callback->proc = &prompt;
    callback->context = NULL;
    ++callback;

    /* termination */
    callback->id = SASL_CB_LIST_END;
    callback->proc = NULL;
    callback->context = NULL;
    ++callback;


      
    if (N_CALLBACKS < callback - callbacks) {
        verror_put_string("Out of callback space; recompile with larger N_CALLBACKS");
	return SASL_FAIL;
    }

    result = sasl_client_init(callbacks);
    if (result != SASL_OK) {
        verror_put_string("Allocating sasl connection state failed");
	return SASL_FAIL;
    }

    result = sasl_client_new(service,
                           fqdn,
                           iplocal,ipremote,
                           NULL, 0,
                           &conn);
    if (result != SASL_OK) {
        verror_put_string("Allocating sasl connection state failed");
	return SASL_FAIL;
    }

    result = sasl_setprop(conn,
                        SASL_SEC_PROPS,
                        &secprops);
    if (result != SASL_OK) {
        verror_put_string("Setting security properties failed");
	return SASL_FAIL;
    }

//    puts("Waiting for mechanism list from server...");

    server_len = recv_response_sasl_data(attrs, &server_response, server_buffer);
    if (server_len < 0) {
       verror_put_string("SASL negotiation failed");
       return SASL_FAIL;
    }

    result = sasl_client_start(conn,
                               mech,
                               NULL,
                               &data,
                               &len,
                               &chosenmech);

    if (result != SASL_OK && result != SASL_CONTINUE) {
        verror_put_string("SASL error: %s\n", sasl_errdetail(conn));
        return SASL_FAIL;
    }

//    printf("Using mechanism %s\n", chosenmech);
    strcpy(server_buffer, chosenmech);
    if (data) {
        if (SASL_BUFFER_SIZE - strlen(server_buffer) - 1 < len) {
            verror_put_string("Not enough buffer space for SASL");
	    return -1;
        }
        memcpy(server_buffer + strlen(server_buffer) + 1, data, len);
        len += strlen(server_buffer) + 1;
        data = NULL;
    } else {
        len = strlen(server_buffer);
    }

    send_response_sasl_data(attrs, &server_response, server_buffer, len);

    authorization_data_free(server_response.authorization_data);

    while (result == SASL_CONTINUE) {
//        puts("Waiting for server reply...");

	server_len = recv_response_sasl_data(attrs, &server_response, server_buffer);
        if (server_len < 0) 
	    return result;

        result = sasl_client_step(conn, server_buffer, server_len, NULL,
                              &data, &len);
        if (result != SASL_OK && result != SASL_CONTINUE) {
            verror_put_string("Performing SASL negotiation failed");
	    return SASL_FAIL;
        }
        if (data && len) {
//            puts("Sending response...");
	    send_response_sasl_data(attrs, &server_response, data, len);
        } else if (result != SASL_OK) {
	    send_response_sasl_data(attrs, &server_response, "", 0);
        }

	authorization_data_free(server_response.authorization_data);
    } 

//    puts("SASL negotiation finished.");

    return result;
}

