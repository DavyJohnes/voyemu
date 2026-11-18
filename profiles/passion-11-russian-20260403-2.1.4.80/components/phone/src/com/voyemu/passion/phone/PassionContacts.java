package com.voyemu.passion.phone;

import android.content.ContentProviderOperation;
import android.content.Context;
import android.provider.ContactsContract;

import java.util.ArrayList;
import java.util.List;

/** Own rows only; the OEM ContactLiveData reads these standard ContactsContract rows. */
final class PassionContacts {
    private static final String TYPE = "com.voyemu.passion.synthetic", ACCOUNT = "Voyemu Phone";
    private static final String OWNED =
            ContactsContract.RawContacts.ACCOUNT_TYPE
                    + "=? AND "
                    + ContactsContract.RawContacts.ACCOUNT_NAME
                    + "=?";

    static void replace(Context context, List<PassionSyntheticPhone.Contact> contacts)
            throws Exception {
        ArrayList<ContentProviderOperation> ops = new ArrayList<ContentProviderOperation>();

        ops.add(
                ContentProviderOperation.newDelete(ContactsContract.RawContacts.CONTENT_URI)
                        .withSelection(OWNED, new String[] {TYPE, ACCOUNT})
                        .build());

        for (PassionSyntheticPhone.Contact contact : contacts) {
            int parent = ops.size();
            ops.add(
                    ContentProviderOperation.newInsert(ContactsContract.RawContacts.CONTENT_URI)
                            .withValue(ContactsContract.RawContacts.ACCOUNT_TYPE, TYPE)
                            .withValue(ContactsContract.RawContacts.ACCOUNT_NAME, ACCOUNT)
                            .withValue(
                                    ContactsContract.RawContacts.AGGREGATION_MODE,
                                    ContactsContract.RawContacts.AGGREGATION_MODE_DISABLED)
                            .build());
            ops.add(
                    ContentProviderOperation.newInsert(ContactsContract.Data.CONTENT_URI)
                            .withValueBackReference(ContactsContract.Data.RAW_CONTACT_ID, parent)
                            .withValue(
                                    ContactsContract.Data.MIMETYPE,
                                    ContactsContract.CommonDataKinds.StructuredName
                                            .CONTENT_ITEM_TYPE)
                            .withValue(
                                    ContactsContract.CommonDataKinds.StructuredName.DISPLAY_NAME,
                                    contact.name)
                            .build());
            ops.add(
                    ContentProviderOperation.newInsert(ContactsContract.Data.CONTENT_URI)
                            .withValueBackReference(ContactsContract.Data.RAW_CONTACT_ID, parent)
                            .withValue(
                                    ContactsContract.Data.MIMETYPE,
                                    ContactsContract.CommonDataKinds.Phone.CONTENT_ITEM_TYPE)
                            .withValue(
                                    ContactsContract.CommonDataKinds.Phone.NUMBER, contact.number)
                            .withValue(
                                    ContactsContract.CommonDataKinds.Phone.TYPE,
                                    ContactsContract.CommonDataKinds.Phone.TYPE_MOBILE)
                            .build());
        }

        context.getContentResolver().applyBatch(ContactsContract.AUTHORITY, ops);
    }

    static void clear(Context context) throws Exception {
        replace(context, java.util.Collections.<PassionSyntheticPhone.Contact>emptyList());
    }
}
