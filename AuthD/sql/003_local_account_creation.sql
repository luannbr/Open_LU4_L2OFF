-- Local account creation shared by password login and the AuthD console.
BEGIN;

CREATE OR REPLACE FUNCTION authd.ap_createaccount(p_account text, p_password bytea)
RETURNS integer
LANGUAGE plpgsql
VOLATILE
AS $$
DECLARE
    created_uid integer;
BEGIN
    IF p_account IS NULL OR p_account COLLATE "C" !~ '^[a-z0-9_]{1,14}$' THEN
        RAISE EXCEPTION 'account must contain 1..14 lowercase ASCII letters, digits or underscores';
    END IF;
    IF p_password IS NULL OR octet_length(p_password) <> 16 THEN
        RAISE EXCEPTION 'password must be a 16-byte type-2 hash';
    END IF;

    -- Never repair partial accounts with an unverified password.
    IF EXISTS (SELECT 1 FROM authd.user_account WHERE account = p_account)
       OR EXISTS (SELECT 1 FROM authd.user_auth WHERE account = p_account)
       OR EXISTS (SELECT 1 FROM authd.account_etc WHERE account = p_account) THEN
        RETURN 0;
    END IF;

    INSERT INTO authd.user_account(account, pay_stat, last_world)
    VALUES (p_account, 2000, 1)
    ON CONFLICT (account) DO NOTHING
    RETURNING uid INTO created_uid;
    IF NOT FOUND THEN
        RETURN 0;
    END IF;

    -- The function call is atomic. Conflicts in either table roll back the account too.
    INSERT INTO authd.user_auth(account, password, new_pwd_flag, otp_flag)
    VALUES (p_account, p_password, 2, 0);
    INSERT INTO authd.account_etc(account) VALUES (p_account);
    RETURN created_uid;
END;
$$;

COMMIT;
