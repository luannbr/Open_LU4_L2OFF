-- Current Cached.exe primary opcode 45 (RequestEstablishFriendship) database
-- slice. The current image fixes two unsigned-integer-compatible inputs and
-- one integer result. Every substantive full-arity-compatible modern donor
-- has the same transactional effect: insert both directed rows with empty
-- memos, commit only when both inserts succeed, and return 1. Otherwise roll
-- back both rows and return 0. The nested exception block is PostgreSQL's
-- statement-local equivalent of that kept procedure transaction.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_establishfriendship"(
    p_char_id integer,
    p_friend_char_id integer
) RETURNS TABLE (
    result integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    BEGIN
        INSERT INTO "world"."user_friend" (
            "char_id",
            "friend_char_id",
            "friend_memo"
        ) VALUES (
            p_char_id,
            p_friend_char_id,
            ''
        );

        INSERT INTO "world"."user_friend" (
            "char_id",
            "friend_char_id",
            "friend_memo"
        ) VALUES (
            p_friend_char_id,
            p_char_id,
            ''
        );
    EXCEPTION WHEN integrity_constraint_violation THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END;

    RETURN QUERY SELECT 1;
END;
$function$;

COMMIT;
