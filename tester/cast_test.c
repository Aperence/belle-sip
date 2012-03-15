/*
	belle-sip - SIP (RFC3261) library.
    Copyright (C) 2010  Belledonne Communications SARL

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "CUnit/Basic.h"

#include "belle-sip/belle-sip.h"

static int init_cast_suite(){
	return 0;
}

static int cleanup_cast_suite(){
	return 0;
}

static void cast_test(){
	belle_sip_stack_t *stack=belle_sip_stack_new(NULL);
	belle_sip_listening_point_t *lp=belle_sip_stack_create_listening_point(stack,"0.0.0.0",5060,"UDP");
	belle_sip_provider_t *provider=belle_sip_stack_create_provider(stack,lp);
	belle_sip_request_t *req=belle_sip_request_new();
	belle_sip_response_t *resp=belle_sip_response_new();
	belle_sip_message_t *msg;
	int tmp;
	
	CU_ASSERT_PTR_NOT_NULL(stack);
	CU_ASSERT_PTR_NOT_NULL(lp);
	CU_ASSERT_PTR_NOT_NULL(provider);
	CU_ASSERT_PTR_NOT_NULL(req);
	CU_ASSERT_PTR_NOT_NULL(resp);
	
	belle_sip_message("Casting belle_sip_request_t to belle_sip_message_t");
	msg=BELLE_SIP_MESSAGE(req);
	CU_ASSERT_PTR_NOT_NULL(msg);
	belle_sip_message("Ok.");
	belle_sip_message("Casting belle_sip_response_t to belle_sip_message_t");
	msg=BELLE_SIP_MESSAGE(resp);
	CU_ASSERT_PTR_NOT_NULL(msg);
	belle_sip_message("Ok.");
	tmp=BELLE_SIP_IS_INSTANCE_OF(req,belle_sip_response_t);
	belle_sip_message("Casting belle_sip_request_t to belle_sip_response_t: %s",tmp ? "yes" : "no");
	CU_ASSERT_EQUAL(tmp,0);
	belle_sip_object_unref(req);
	belle_sip_object_unref(resp);
	belle_sip_object_unref(provider);
	belle_sip_object_unref(stack);
}


int belle_sip_cast_test_suite(){
	CU_pSuite pSuite = CU_add_suite("Object inheritence", init_cast_suite, cleanup_cast_suite);

	if (NULL == CU_add_test(pSuite, "casting requests and responses", cast_test)) {
		return CU_get_error();
	}
	return 0;
}

