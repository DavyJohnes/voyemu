package com.qinggan.account;

/** OEM account of the head unit; a test selects a guest or a logged-in user. */
public final class AccountUserManager {
    public static volatile boolean guest = true;
    private static final AccountUserManager INSTANCE = new AccountUserManager();

    public static AccountUserManager getInstance() {
        return INSTANCE;
    }

    public AccountInfoBean AccountInfoBean() {
        // checkstyle-allow MethodName: OEM account donor method name
        AccountInfoBean account = new AccountInfoBean();

        account.isGuest = guest;

        return account;
    }
}
